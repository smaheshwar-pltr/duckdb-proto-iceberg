/// Conversion between DuckDB S3 secret options and iceberg-cpp S3 properties.

#pragma once

#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/types/value.hpp"

#include <string>
#include <unordered_map>

namespace duckdb::conversion {

/// Converts the options of a DuckDB S3 secret to iceberg-cpp S3 properties.
[[nodiscard]] std::unordered_map<std::string, std::string>
ConvertS3SecretToIcebergProperties(const case_insensitive_tree_t<Value> &secret);

/// Converts iceberg-cpp S3 properties to DuckDB S3 secret options.
///
/// Iceberg's s3.endpoint is a URI whose http(s) scheme, if present, determines SSL and takes precedence over
/// s3.ssl.enabled. DuckDB's endpoint is scheme-less, with SSL set by use_ssl.
[[nodiscard]] case_insensitive_map_t<Value>
ConvertIcebergPropertiesToS3Secret(const std::unordered_map<std::string, std::string> &properties);

} // namespace duckdb::conversion
