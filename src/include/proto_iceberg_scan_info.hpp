#pragma once

#include "duckdb/function/table_function.hpp"

#include <memory>
#include <variant>

namespace iceberg {
class Table;
class Schema;
} // namespace iceberg

namespace duckdb {

/// Reads the table's current snapshot and schema.
struct CurrentRead {};

/// Reads a specific snapshot with that snapshot's schema.
struct PinnedRead {
	int64_t snapshot_id;
};

/// Which version of the table to read.
using ScanMode = std::variant<CurrentRead, PinnedRead>;

/// What a table scan needs from the catalog.
struct ProtoIcebergScanInfo : TableFunctionInfo {
	ProtoIcebergScanInfo(std::shared_ptr<iceberg::Table> table_p, ScanMode mode_p,
	                     std::shared_ptr<iceberg::Schema> schema_p)
	    : table(std::move(table_p)), mode(std::move(mode_p)), schema(std::move(schema_p)) {
	}

	/// Shared by the transaction's concurrent scans of the table, so only read from it.
	std::shared_ptr<iceberg::Table> table;
	ScanMode mode;

	/// Must be the schema iceberg-cpp resolves for this read (the snapshot's for PinnedRead, the current one for
	/// CurrentRead), since iceberg-cpp binds pushed-down filters' column names against its own.
	std::shared_ptr<iceberg::Schema> schema;
};

} // namespace duckdb
