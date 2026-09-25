#pragma once

#include <set>
#include <string>

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

/// DuckDB secret types, and the provider name secret types use for secrets created from explicit options.
inline const std::string kIcebergSecretType = "iceberg";
inline const std::string kS3SecretType = "s3";
inline const std::string kConfigProvider = "config";

inline const std::string kBearerPrefix = "Bearer ";

} // namespace duckdb::constants
