/// Adapter utilities for bridging iceberg-cpp and DuckDB types.

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

namespace duckdb::adapters {

/// Constructs an iceberg-cpp Namespace from a single-level namespace name.
inline iceberg::Namespace GetNamespace(std::string_view namespace_name) {
	return iceberg::Namespace {.levels = {std::string(namespace_name)}};
}

/// Constructs an iceberg-cpp TableIdentifier from a single-level namespace name and table name.
inline iceberg::TableIdentifier GetTableIdentifier(std::string_view namespace_name, std::string_view table_name) {
	return iceberg::TableIdentifier {.ns = GetNamespace(namespace_name), .name = std::string(table_name)};
}

/// Maps an iceberg-cpp Type to the corresponding DuckDB LogicalType.
/// Throws InvalidInputException if an unsupported TypeId is encountered.
[[nodiscard]] LogicalType MapIcebergType(const iceberg::Type &type);

/// Converts a DuckDB Value to an iceberg-cpp Literal.
/// Returns std::nullopt for unsupported types.
[[nodiscard]] std::optional<iceberg::Literal> ConvertValueToLiteral(const Value &value);

/// Builds a MultiFileColumnDefinition from an Iceberg SchemaField, including
/// recursive child field IDs for nested types (STRUCT, LIST, MAP).
/// The top-level definition gets its identifier set to the field's field_id,
/// and nested children are populated recursively.
[[nodiscard]] MultiFileColumnDefinition BuildColumnDefinition(const iceberg::SchemaField &field);

/// Builds the DuckDB column list for an Iceberg schema via BuildColumnDefinition per top-level field.
[[nodiscard]] vector<MultiFileColumnDefinition> BuildColumnList(const iceberg::Schema &schema);

/// Conservatively translates a TableFilterSet into an iceberg-cpp Expression.
/// Column indices are resolved via the schema's field list. Untranslatable
/// filters widen to AlwaysTrue(), so the result is guaranteed to be no narrower
/// than the original — suitable for scan planning where over-reading is safe
/// but under-reading is not.
[[nodiscard]] std::shared_ptr<iceberg::Expression> TranslateOrWidenFilters(const TableFilterSet &filters,
                                                                           const iceberg::Schema &schema);

/// Conservatively translates a single DuckDB TableFilter into an iceberg-cpp
/// Expression. Unsupported filter types or value conversions widen to
/// AlwaysTrue(), preserving the invariant that the result never excludes rows
/// the original filter would have kept.
[[nodiscard]] std::shared_ptr<iceberg::Expression> TranslateOrWidenFilter(const TableFilter &filter,
                                                                          const iceberg::SchemaField &field);

} // namespace duckdb::adapters
