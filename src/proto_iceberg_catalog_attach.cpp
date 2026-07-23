#include "constants.hpp"
#include "unwrap.hpp"
#include "proto_iceberg_catalog.hpp"

#include "duckdb/common/types/value.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/secret/secret.hpp"

#include "iceberg/catalog/rest/catalog_properties.h"

#include <unordered_map>

namespace duckdb {

namespace {

using constants::kBearerPrefix;
using constants::kDefaultNamespace;
using constants::kDefaultSchema;
using constants::kEndpoint;
using constants::kIcebergSecretType;
using constants::kToken;
using constants::kWarehouse;

const string kUri = "uri";
const string kHeaderAuthorization = "header.Authorization";

namespace s3 = constants::s3;

/// Reads DuckDB's S3 secret and converts it to iceberg-cpp S3 properties, so that
/// the iceberg-cpp REST catalog then uses it as base / default IO properties.
unordered_map<string, string> ReadS3SecretAsIcebergProperties(ClientContext &context) {
	auto &secret_manager = SecretManager::Get(context);
	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
	auto secret_match = secret_manager.LookupSecret(transaction, s3::kSecretScope, s3::kSecretType);
	if (!secret_match.HasMatch()) {
		return {};
	}

	auto *kv_secret = dynamic_cast<const KeyValueSecret *>(&secret_match.GetSecret());
	if (!kv_secret) {
		return {};
	}

	unordered_map<string, string> props;
	for (const auto &[duckdb_key, iceberg_key, kind] : s3::kPropertyMappings) {
		if (Value value; kv_secret->TryGetValue(string(duckdb_key), value)) {
			switch (kind) {
			case s3::PropertyKind::kPlain:
				props[string(iceberg_key)] = value.ToString();
				break;
			case s3::PropertyKind::kPathStyle:
				if (value.ToString() == s3::kUrlStylePath) {
					props[string(iceberg_key)] = "true";
				}
				break;
			case s3::PropertyKind::kSsl:
				props[string(iceberg_key)] =
				    BooleanValue::Get(value.DefaultCastAs(LogicalType::BOOLEAN)) ? "true" : "false";
				break;
			}
		}
	}
	return props;
}

/// User-provided options to connect to the REST catalog.
struct CatalogParams {
	string uri;
	string warehouse;
	string token;
	string default_schema;
};

/// Parses user-provided ATTACH options into CatalogParams.
CatalogParams ParseAttachOptions(AttachInfo &info) {
	CatalogParams params;
	for (const auto &[key, value] : info.options) {
		if (auto lower_key = StringUtil::Lower(key); lower_key == kEndpoint) {
			params.uri = value.ToString();
		} else if (lower_key == kWarehouse) {
			params.warehouse = value.ToString();
		} else if (lower_key == kToken) {
			params.token = value.ToString();
		} else if (lower_key == kDefaultSchema) {
			params.default_schema = value.ToString();
		} else if (lower_key != "type" && lower_key != "read_only") {
			throw BinderException("Unrecognized ATTACH option: '%s'", key);
		}
	}
	if (params.warehouse.empty() && !info.path.empty()) {
		// For consistency with duckdb-iceberg, use the path as the warehouse, not the REST catalog URI.
		params.warehouse = info.path;
	}
	return params;
}

/// Fills in secret CatalogParams not set inline from an ICEBERG secret.
void MergeIcebergSecretParams(CatalogParams &params, ClientContext &context, const string &name) {
	auto &secret_manager = SecretManager::Get(context);
	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);

	auto apply = [&params](const KeyValueSecret &kv) {
		Value val;
		if (params.uri.empty() && kv.TryGetValue(kEndpoint, val)) {
			params.uri = val.ToString();
		}
		if (params.token.empty() && kv.TryGetValue(kToken, val)) {
			params.token = val.ToString();
		}
	};

	// Prefer a secret named like the ATTACH alias, falling back to a type-only secret.
	if (auto entry = secret_manager.GetSecretByName(transaction, name);
	    entry && entry->secret && entry->secret->GetType() == kIcebergSecretType) {
		if (auto *kv = dynamic_cast<const KeyValueSecret *>(entry->secret.get())) {
			apply(*kv);
		}
	} else if (auto match = secret_manager.LookupSecret(transaction, "", kIcebergSecretType); match.HasMatch()) {
		if (auto *kv = dynamic_cast<const KeyValueSecret *>(&match.GetSecret())) {
			apply(*kv);
		}
	}
}

} // namespace

unique_ptr<Catalog> ProtoIcebergCatalog::Attach(optional_ptr<StorageExtensionInfo>, ClientContext &context,
                                                AttachedDatabase &db, const string &name, AttachInfo &info,
                                                AttachOptions &) {
	auto params = ParseAttachOptions(info);
	MergeIcebergSecretParams(params, context, name);
	if (params.default_schema.empty()) {
		params.default_schema = kDefaultNamespace;
	}
	if (params.uri.empty()) {
		throw InvalidConfigurationException("Missing 'endpoint' option for Iceberg attach");
	}
	StringUtil::RTrim(params.uri, "/");

	// Create iceberg-cpp REST catalog properties. Include DuckDB S3 secrets here; iceberg-cpp will automatically
	// fall back to using these as file IO properties if they are not vended by the REST catalog, as we desire.
	auto catalog_props = ReadS3SecretAsIcebergProperties(context);
	catalog_props[kUri] = params.uri;
	if (!params.warehouse.empty()) {
		catalog_props[kWarehouse] = params.warehouse;
	}
	if (!params.token.empty()) {
		catalog_props[kHeaderAuthorization] = kBearerPrefix + params.token;
	}

	auto config = iceberg::rest::RestCatalogProperties::FromMap(catalog_props);
	auto rest_catalog = UnwrapOrThrow(iceberg::rest::RestCatalog::Make(config),
	                                  "Failed to create Iceberg REST catalog at %s", params.uri);

	auto catalog = make_uniq<ProtoIcebergCatalog>(db, std::move(params.uri), std::move(rest_catalog),
	                                              std::move(params.default_schema));
	return std::move(catalog);
}

} // namespace duckdb
