#pragma once

#include "duckdb/function/table_function.hpp"

#include <memory>
#include <variant>

namespace iceberg {
class Table;
class Schema;
} // namespace iceberg

namespace duckdb {

/// Indicates that iceberg-cpp should read the table in its current state (snapshot and schema).
struct CurrentRead {};

/// Indicates that iceberg-cpp should read at a specific snapshot (and that schema).
struct PinnedRead {
	int64_t snapshot_id;
};

/// Mode indicating what version of the table to read.
using ScanMode = std::variant<CurrentRead, PinnedRead>;

/// Carries information required for a table read from the catalog layer to the scan layer.
struct ProtoIcebergScanInfo : TableFunctionInfo {
	ProtoIcebergScanInfo(std::shared_ptr<iceberg::Table> table_p, ScanMode mode_p,
	                     std::shared_ptr<iceberg::Schema> schema_p)
	    : table(std::move(table_p)), mode(std::move(mode_p)), schema(std::move(schema_p)) {
	}

	std::shared_ptr<iceberg::Table> table;
	ScanMode mode;

	/// Caches the Iceberg schema for this scan.
	/// Precondition: This must be the schema that iceberg-cpp will independently resolve for this read during scan
	/// planning. For PinnedRead, this is the snapshot's schema; for CurrentRead, this is the table's current schema.
	std::shared_ptr<iceberg::Schema> schema;
};

} // namespace duckdb
