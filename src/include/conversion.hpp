/// Conversion utilities between iceberg-cpp and DuckDB types, values and filters.

#pragma once

#include "duckdb/common/multi_file/multi_file_data.hpp"
#include "duckdb/common/types.hpp"

#include "iceberg/expression/expression.h"
#include "iceberg/expression/literal.h"
#include "iceberg/schema.h"
#include "iceberg/schema_field.h"
#include "iceberg/table_identifier.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace duckdb {
class TableFilter;
class TableFilterSet;
} // namespace duckdb

namespace duckdb::conversion {

/// Constructs an iceberg-cpp Namespace from a single-level namespace name.
inline iceberg::Namespace GetNamespace(std::string_view namespace_name) {
	return iceberg::Namespace {.levels = {std::string(namespace_name)}};
}

/// Constructs an iceberg-cpp TableIdentifier from a single-level namespace name and table name.
inline iceberg::TableIdentifier GetTableIdentifier(std::string_view namespace_name, std::string_view table_name) {
	return iceberg::TableIdentifier {.ns = GetNamespace(namespace_name), .name = std::string(table_name)};
}

/// Maps an iceberg-cpp type to a DuckDB type, throwing InvalidInputException if it's unsupported.
[[nodiscard]] LogicalType MapIcebergType(const iceberg::Type &type);

/// Converts a DuckDB value to an iceberg-cpp literal, or nullopt if it's NULL or its type is unsupported.
[[nodiscard]] std::optional<iceberg::Literal> ConvertValueToLiteral(const Value &value);

/// Builds a column definition identified by the field's ID, with nested fields identified the same way.
[[nodiscard]] MultiFileColumnDefinition BuildColumnDefinition(const iceberg::SchemaField &field);

/// Builds a column definition for each top-level field of the schema.
[[nodiscard]] vector<MultiFileColumnDefinition> BuildColumnList(const iceberg::Schema &schema);

/// Translates filters, keyed by index into the schema's fields, for scan planning. Untranslatable filters widen to
/// AlwaysTrue, so the result never excludes rows the filters keep.
[[nodiscard]] std::shared_ptr<iceberg::Expression> TranslateOrWidenFilters(const TableFilterSet &filters,
                                                                           const iceberg::Schema &schema);

/// Translates a single filter, widening whatever it can't translate to AlwaysTrue.
[[nodiscard]] std::shared_ptr<iceberg::Expression> TranslateOrWidenFilter(const TableFilter &filter,
                                                                          const iceberg::SchemaField &field);

} // namespace duckdb::conversion
