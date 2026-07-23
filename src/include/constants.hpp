#pragma once

#include "iceberg/arrow/s3/s3_properties.h"

#include <array>
#include <set>
#include <string>
#include <string_view>

// N.B. Though our extension is C++23, we use std::string instead of std::string_view
// for constants to interface more easily with DuckDB APIs that expect std::string.
namespace duckdb::constants {

/// ATTACH and ICEBERG secret option keys.
inline const std::string kEndpoint = "endpoint";
inline const std::string kWarehouse = "warehouse";
inline const std::string kToken = "token";
inline const std::string kDefaultSchema = "default_schema";

/// Fall back to "default" namespace, matching duckdb-iceberg.
inline const std::string kDefaultNamespace = "default";

inline const std::set kRedactedSecrets = {kToken};

inline const std::string kIcebergSecretType = "iceberg";

inline const std::string kBearerPrefix = "Bearer ";

/// DuckDB S3 secret key names, used when reading user-created S3 secrets
/// and when creating scoped S3 secrets from (possibly vended) table IO properties.
namespace s3 {

inline const std::string kSecretType = "s3";
inline const std::string kSecretScope = "s3://";
inline const std::string kProvider = "config";
inline const std::string kKeyId = "key_id";
inline const std::string kSecret = "secret";
inline const std::string kSessionToken = "session_token";
inline const std::string kRegion = "region";
inline const std::string kEndpoint = "endpoint";
inline const std::string kUrlStyle = "url_style";
inline const std::string kUseSsl = "use_ssl";
inline const std::string kUrlStylePath = "path";

enum class PropertyKind { kPlain, kPathStyle, kSsl };

struct PropertyMapping {
	std::string_view duckdb_key;
	std::string_view iceberg_key;
	PropertyKind kind;
};

/// Maps DuckDB and iceberg-cpp S3 configuration correspondence bidirectionally.
inline const std::array<PropertyMapping, 7> kPropertyMappings = {{
    // Key and value correspondence, passed through verbatim
    {kKeyId, iceberg::arrow::S3Properties::kAccessKeyId, PropertyKind::kPlain},
    {kSecret, iceberg::arrow::S3Properties::kSecretAccessKey, PropertyKind::kPlain},
    {kSessionToken, iceberg::arrow::S3Properties::kSessionToken, PropertyKind::kPlain},
    {kRegion, iceberg::arrow::S3Properties::kRegion, PropertyKind::kPlain},
    {kEndpoint, iceberg::arrow::S3Properties::kEndpoint, PropertyKind::kPlain},
    // Key but not value correspondence, special-cased
    {kUrlStyle, iceberg::arrow::S3Properties::kPathStyleAccess, PropertyKind::kPathStyle},
    {kUseSsl, iceberg::arrow::S3Properties::kSslEnabled, PropertyKind::kSsl},
}};

} // namespace s3

} // namespace duckdb::constants
