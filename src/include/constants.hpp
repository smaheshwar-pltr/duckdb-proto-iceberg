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
inline const std::string kUrlStyleVhost = "vhost";

struct PropertyMapping {
	std::string_view duckdb_key;
	std::string_view iceberg_key;
};

/// DuckDB and iceberg-cpp S3 keys whose values correspond verbatim. Endpoint, URL style and SSL values differ in
/// format and are converted in conversion.hpp.
inline const std::array<PropertyMapping, 4> kPlainPropertyMappings = {{
    {kKeyId, iceberg::arrow::S3Properties::kAccessKeyId},
    {kSecret, iceberg::arrow::S3Properties::kSecretAccessKey},
    {kSessionToken, iceberg::arrow::S3Properties::kSessionToken},
    {kRegion, iceberg::arrow::S3Properties::kRegion},
}};

} // namespace s3

} // namespace duckdb::constants
