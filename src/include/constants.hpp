#pragma once

#include <set>
#include <string>

// std::string rather than std::string_view, since DuckDB's APIs take std::string.
namespace duckdb::constants {

/// ATTACH and ICEBERG secret option keys.
inline const std::string kEndpoint = "endpoint";
inline const std::string kWarehouse = "warehouse";
inline const std::string kToken = "token";
inline const std::string kDefaultSchema = "default_schema";

/// The default schema when none is configured, as in duckdb-iceberg.
inline const std::string kDefaultNamespace = "default";

inline const std::set kRedactedSecrets = {kToken};

/// DuckDB secret types, and the provider name for secrets created from explicit options.
inline const std::string kIcebergSecretType = "iceberg";
inline const std::string kS3SecretType = "s3";
inline const std::string kConfigProvider = "config";

inline const std::string kBearerPrefix = "Bearer ";

} // namespace duckdb::constants
