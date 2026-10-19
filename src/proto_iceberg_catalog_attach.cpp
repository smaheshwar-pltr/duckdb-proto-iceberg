#include "constants.hpp"
#include "s3_conversion.hpp"
#include "unwrap.hpp"
#include "proto_iceberg_catalog.hpp"

#include "duckdb/common/types/value.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/secret/secret.hpp"

#include "iceberg/catalog/rest/catalog_properties.h"
#include "iceberg/catalog/rest/rest_catalog.h"

#include <unordered_map>

namespace duckdb {

namespace {

using constants::kBearerPrefix;
using constants::kDefaultNamespace;
using constants::kDefaultSchema;
using constants::kEndpoint;
using constants::kIcebergSecretType;
using constants::kS3SecretType;
using constants::kToken;
using constants::kWarehouse;

const string kUri = "uri";
const string kHeaderAuthorization = "header.Authorization";
/// Matches only S3 secrets scoped to all of S3, not per-bucket ones or the per-table secrets we create.
const string kS3RootPath = "s3://";

/// Returns DuckDB's S3 secret as iceberg-cpp S3 properties.
unordered_map<string, string> ReadS3SecretAsIcebergProperties(ClientContext &context) {
	auto &secret_manager = SecretManager::Get(context);
	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
	auto secret_match = secret_manager.LookupSecret(transaction, kS3RootPath, kS3SecretType);
	if (!secret_match.HasMatch()) {
		return {};
	}

	auto *kv_secret = dynamic_cast<const KeyValueSecret *>(&secret_match.GetSecret());
	if (!kv_secret) {
		return {};
	}

	return conversion::ConvertS3SecretToIcebergProperties(kv_secret->secret_map);
}

/// User-provided options to connect to the REST catalog.
struct CatalogParams {
	string uri;
	string warehouse;
	string token;
	string default_schema;
};

/// Parses our ATTACH options; DuckDB has already consumed its generic ones (TYPE, READ_ONLY, ...).
CatalogParams ParseAttachOptions(const AttachInfo &info, const AttachOptions &options) {
	CatalogParams params;
	for (const auto &[key, value] : options.options) {
		if (auto lower_key = StringUtil::Lower(key); lower_key == kEndpoint) {
			params.uri = value.ToString();
		} else if (lower_key == kWarehouse) {
			params.warehouse = value.ToString();
		} else if (lower_key == kToken) {
			params.token = value.ToString();
		} else if (lower_key == kDefaultSchema) {
			params.default_schema = value.ToString();
		} else {
			throw BinderException("Unrecognized ATTACH option: '%s'", key);
		}
	}
	if (params.warehouse.empty() && !info.path.empty()) {
		// As in duckdb-iceberg, the path is the warehouse, not the catalog URI.
		params.warehouse = info.path;
	}
	return params;
}

/// Fills in the endpoint and token from an ICEBERG secret where ATTACH didn't set them.
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
                                                AttachOptions &options) {
	auto params = ParseAttachOptions(info, options);
	MergeIcebergSecretParams(params, context, name);
	if (params.default_schema.empty()) {
		params.default_schema = kDefaultNamespace;
	}
	if (params.uri.empty()) {
		throw InvalidConfigurationException("Missing 'endpoint' option for Iceberg attach");
	}
	StringUtil::RTrim(params.uri, "/");

	// iceberg-cpp uses the S3 secret's properties as default file IO properties, overridden by any the catalog vends.
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
	return catalog;
}

} // namespace duckdb
