#include "s3_conversion.hpp"

#include "iceberg/arrow/s3/s3_properties.h"
#include "iceberg/util/property_util.h"

#include <array>
#include <optional>
#include <string_view>

namespace duckdb::conversion {
namespace {

using iceberg::arrow::S3Properties;

// DuckDB S3 secret option names and values.
const string kEndpoint = "endpoint";
const string kUrlStyle = "url_style";
const string kUseSsl = "use_ssl";
const string kUrlStylePath = "path";
const string kUrlStyleVhost = "vhost";

constexpr std::string_view kHttpScheme = "http://";
constexpr std::string_view kHttpsScheme = "https://";

struct PlainPropertyMapping {
	std::string_view duckdb_key;
	std::string_view iceberg_key;
};

/// DuckDB secret options and iceberg-cpp properties whose values mean the same in both.
constexpr std::array<PlainPropertyMapping, 4> kPlainPropertyMappings = {{
    {"key_id", S3Properties::kAccessKeyId},
    {"secret", S3Properties::kSecretAccessKey},
    {"session_token", S3Properties::kSessionToken},
    {"region", S3Properties::kClientRegion},
}};

/// Returns whether an Iceberg S3 endpoint's http(s) scheme implies SSL, or nullopt if it has no such scheme.
std::optional<bool> SchemeUsesSsl(std::string_view endpoint) {
	if (endpoint.starts_with(kHttpsScheme)) {
		return true;
	}
	if (endpoint.starts_with(kHttpScheme)) {
		return false;
	}
	return std::nullopt;
}

/// Rewrites an S3 scheme alias such as s3a:// or S3:// to s3://, as iceberg-cpp does before matching credential
/// prefixes.
std::string CanonicalizeS3Scheme(std::string_view location) {
	if (auto separator = location.find("://");
	    separator != std::string_view::npos && iceberg::arrow::IsS3Scheme(location.substr(0, separator))) {
		return "s3://" + std::string(location.substr(separator + 3));
	}
	return std::string(location);
}

/// Returns DuckDB's scheme-less form of an Iceberg S3 endpoint, e.g. host:9000/path for http://host:9000/path/.
std::string_view ToDuckDBEndpoint(std::string_view endpoint) {
	if (endpoint.starts_with(kHttpsScheme)) {
		endpoint.remove_prefix(kHttpsScheme.size());
	} else if (endpoint.starts_with(kHttpScheme)) {
		endpoint.remove_prefix(kHttpScheme.size());
	}
	// httpfs appends the request path to the endpoint's path, so a trailing slash would double up.
	while (endpoint.ends_with('/')) {
		endpoint.remove_suffix(1);
	}
	return endpoint;
}

} // namespace

std::unordered_map<std::string, std::string>
ConvertS3SecretToIcebergProperties(const case_insensitive_tree_t<Value> &secret) {
	auto get = [&secret](std::string_view key) -> std::optional<Value> {
		if (auto it = secret.find(string(key)); it != secret.end() && !it->second.IsNull()) {
			return it->second;
		}
		return std::nullopt;
	};
	auto get_string = [&get](std::string_view key) -> string {
		auto value = get(key);
		return value ? value->ToString() : string();
	};

	std::unordered_map<std::string, std::string> properties;
	for (const auto &[duckdb_key, iceberg_key] : kPlainPropertyMappings) {
		if (auto value = get_string(duckdb_key); !value.empty()) {
			properties[string(iceberg_key)] = std::move(value);
		}
	}
	if (auto url_style = get_string(kUrlStyle); url_style == kUrlStylePath) {
		properties[string(S3Properties::kPathStyleAccess)] = "true";
	} else if (url_style == kUrlStyleVhost) {
		properties[string(S3Properties::kPathStyleAccess)] = "false";
	}
	// DuckDB's scheme-less endpoint passes through: iceberg-cpp hands it and s3.ssl.enabled to Arrow, which passes
	// them to the AWS SDK, which prefixes a scheme-less endpoint with the scheme s3.ssl.enabled selects.
	if (auto endpoint = get_string(kEndpoint); !endpoint.empty()) {
		properties[string(S3Properties::kEndpoint)] = std::move(endpoint);
	}
	if (auto use_ssl = get(kUseSsl)) {
		properties[string(S3Properties::kSslEnabled)] =
		    BooleanValue::Get(use_ssl->DefaultCastAs(LogicalType::BOOLEAN)) ? "true" : "false";
	}
	return properties;
}

case_insensitive_map_t<Value>
ConvertIcebergPropertiesToS3Secret(const std::unordered_map<std::string, std::string> &properties) {
	auto get = [&properties](std::string_view key) -> std::string_view {
		if (auto it = properties.find(string(key)); it != properties.end()) {
			return it->second;
		}
		return {};
	};

	case_insensitive_map_t<Value> options;
	for (const auto &[duckdb_key, iceberg_key] : kPlainPropertyMappings) {
		if (auto value = get(iceberg_key); !value.empty()) {
			options[string(duckdb_key)] = Value(string(value));
		}
	}
	// iceberg-cpp reads a present boolean as true only if it is "true", ignoring case.
	if (auto path_style =
	        iceberg::PropertyUtil::PropertyAsOptionalBoolean(properties, S3Properties::kPathStyleAccess)) {
		options[kUrlStyle] = Value(*path_style ? kUrlStylePath : kUrlStyleVhost);
	}
	auto endpoint = get(S3Properties::kEndpoint);
	if (auto duckdb_endpoint = ToDuckDBEndpoint(endpoint); !duckdb_endpoint.empty()) {
		options[kEndpoint] = Value(string(duckdb_endpoint));
	}
	// s3.ssl.enabled takes precedence over the endpoint's scheme, as in iceberg-cpp.
	if (auto use_ssl =
	        iceberg::PropertyUtil::PropertyAsOptionalBoolean(properties, S3Properties::kSslEnabled).or_else([&] {
		        return SchemeUsesSsl(endpoint);
	        })) {
		options[kUseSsl] = Value::BOOLEAN(*use_ssl);
	}
	return options;
}

std::unordered_map<std::string, std::string>
MergeStorageCredential(std::unordered_map<std::string, std::string> properties,
                       std::span<const iceberg::StorageCredential> credentials, std::string_view location) {
	const auto canonical_location = CanonicalizeS3Scheme(location);
	const iceberg::StorageCredential *best = nullptr;
	size_t best_length = 0;
	for (const auto &credential : credentials) {
		if (!iceberg::arrow::IsS3CredentialPrefix(credential.prefix)) {
			continue;
		}
		auto prefix = CanonicalizeS3Scheme(credential.prefix);
		if (prefix.size() > best_length && canonical_location.starts_with(prefix)) {
			best = &credential;
			best_length = prefix.size();
		}
	}
	if (best) {
		for (const auto &[key, value] : best->config) {
			properties.insert_or_assign(key, value);
		}
	}
	return properties;
}

} // namespace duckdb::conversion
