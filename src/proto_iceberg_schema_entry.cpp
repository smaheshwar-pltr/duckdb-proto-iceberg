#include "conversion.hpp"
#include "unwrap.hpp"
#include "proto_iceberg_schema_entry.hpp"
#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_table_entry.hpp"
#include "proto_iceberg_transaction.hpp"
#include "proto_iceberg_scan_info.hpp"

#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/transaction_exception.hpp"
#include "duckdb/logging/logger.hpp"

#include "iceberg/table.h"
#include "iceberg/schema.h"
#include "iceberg/schema_field.h"
#include "iceberg/util/snapshot_util.h"
#include "iceberg/util/timepoint.h"

namespace duckdb {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};

unique_ptr<ProtoIcebergTableEntry> CreateTableEntry(ProtoIcebergCatalog &ic_catalog, ProtoIcebergSchemaEntry &schema,
                                                    shared_ptr<ProtoIcebergScanInfo> scan_info,
                                                    const string &entry_name) {
	auto create_info = make_uniq<CreateTableInfo>();
	create_info->table = entry_name;
	for (auto &field : scan_info->schema->fields()) {
		ColumnDefinition col(string(field.name()), conversion::MapIcebergType(*field.type()));
		if (!field.doc().empty()) {
			col.SetComment(Value(string(field.doc())));
		}
		create_info->columns.AddColumn(std::move(col));
	}
	return make_uniq<ProtoIcebergTableEntry>(ic_catalog, schema, *create_info, std::move(scan_info));
}

/// Resolves the table's state as of the transaction's start, or throws TransactionException if it isn't reachable.
shared_ptr<ProtoIcebergScanInfo> ResolveScanInfo(ClientContext &context, ProtoIcebergTransaction &txn,
                                                 std::shared_ptr<iceberg::Table> table, const string &namespace_name,
                                                 const string &table_name) {
	// If the table changed since the transaction started, read the snapshot current at that point, with that
	// snapshot's schema, which may be older than the table's schema at that point.
	if (auto txn_start = iceberg::TimePointMsFromUnixMs(txn.GetStartTimestampMs());
	    !table->snapshots().empty() && table->last_updated_ms() > txn_start) {
		auto maybe_snapshot_id = iceberg::SnapshotUtil::OptionalSnapshotIdAsOfTime(*table, txn_start);
		if (!maybe_snapshot_id.has_value()) {
			// No snapshot was current at the transaction's start, or it has since expired.
			throw TransactionException(
			    "Table '%s.%s' does not have a reachable state in this transaction; please restart your transaction",
			    namespace_name, table_name);
		}
		auto snapshot_id = maybe_snapshot_id.value();
		DUCKDB_LOG_DEBUG(context, "proto_iceberg: pinning read of '%s.%s' to snapshot %lld", namespace_name, table_name,
		                 snapshot_id);
		auto schema = UnwrapOrThrow(iceberg::SnapshotUtil::SchemaFor(*table, snapshot_id),
		                            "Failed to resolve schema for snapshot %lld of table '%s.%s'", snapshot_id,
		                            namespace_name, table_name);
		return make_shared_ptr<ProtoIcebergScanInfo>(std::move(table), ScanMode::PinnedRead {snapshot_id},
		                                             std::move(schema));
	}

	// Unchanged or snapshot-less: read the current state. For a snapshot-less table, finding the schema current at
	// the transaction's start would mean reading earlier metadata files.
	auto schema = UnwrapOrThrow(table->schema(), "Failed to read schema for table '%s.%s'", namespace_name, table_name);
	return make_shared_ptr<ProtoIcebergScanInfo>(std::move(table), ScanMode::CurrentRead {}, std::move(schema));
}

} // namespace

ProtoIcebergTableEntry &ProtoIcebergSchemaEntry::TableRegistry::Store(unique_ptr<ProtoIcebergTableEntry> entry) {
	auto &stored = *entries_.emplace_back(std::move(entry));
	index_.Put(stored.name, stored);
	return stored;
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::LoadTableEntry(ProtoIcebergTransaction &txn,
                                                                   const string &table_name, ClientContext &context,
                                                                   Mutex<TableRegistry>::Guard &tables) {
	auto &ic_catalog = GetIcebergCatalog();
	DUCKDB_LOG_DEBUG(context, "proto_iceberg: LoadTable '%s.%s'", name, table_name);
	auto table_result = (*ic_catalog.LockCatalog())->LoadTable(conversion::GetTableIdentifier(name, table_name));

	// A missing table or namespace returns no entry; other errors throw.
	if (!table_result.has_value()) {
		const auto &[kind, message] = table_result.error();
		if (bool namespace_missing = kind == iceberg::ErrorKind::kNoSuchNamespace;
		    namespace_missing || kind == iceberg::ErrorKind::kNoSuchTable) {
			if (namespace_missing) {
				MarkNamespaceMissing();
			}
			tables->MarkMissing(table_name);
			return nullptr;
		}
		throw IOException("Failed to load table '%s.%s': %s", name, table_name, message);
	}

	// LoadTable initialized Arrow S3, so S3 cleanup can now be registered.
	ProtoIcebergCatalog::RegisterS3Finalizer();

	auto scan_info = ResolveScanInfo(context, txn, std::move(table_result.value()), name, table_name);
	auto table_entry = CreateTableEntry(ic_catalog, *this, std::move(scan_info), table_name);
	return &tables->Store(std::move(table_entry));
}

ProtoIcebergSchemaEntry::ProtoIcebergSchemaEntry(Catalog &catalog, CreateSchemaInfo &info)
    : SchemaCatalogEntry(catalog, info) {
}

ProtoIcebergSchemaEntry::~ProtoIcebergSchemaEntry() = default;

ProtoIcebergCatalog &ProtoIcebergSchemaEntry::GetIcebergCatalog() const {
	return catalog.Cast<ProtoIcebergCatalog>();
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateTable(CatalogTransaction transaction,
                                                                BoundCreateTableInfo &info) {
	throw NotImplementedException("CREATE TABLE is not supported in a read-only Iceberg catalog");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateFunction(CatalogTransaction transaction,
                                                                   CreateFunctionInfo &info) {
	throw BinderException("Iceberg databases do not support creating functions");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateIndex(CatalogTransaction transaction, CreateIndexInfo &info,
                                                                TableCatalogEntry &table) {
	throw NotImplementedException("CREATE INDEX is not supported in a read-only Iceberg catalog");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateView(CatalogTransaction transaction, CreateViewInfo &info) {
	throw NotImplementedException("CREATE VIEW is not supported in a read-only Iceberg catalog");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateSequence(CatalogTransaction transaction,
                                                                   CreateSequenceInfo &info) {
	throw BinderException("Iceberg databases do not support creating sequences");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateTableFunction(CatalogTransaction transaction,
                                                                        CreateTableFunctionInfo &info) {
	throw BinderException("Iceberg databases do not support creating table functions");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateCopyFunction(CatalogTransaction transaction,
                                                                       CreateCopyFunctionInfo &info) {
	throw BinderException("Iceberg databases do not support creating copy functions");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreatePragmaFunction(CatalogTransaction transaction,
                                                                         CreatePragmaFunctionInfo &info) {
	throw BinderException("Iceberg databases do not support creating pragma functions");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateType(CatalogTransaction transaction, CreateTypeInfo &info) {
	throw BinderException("Iceberg databases do not support creating types");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::CreateCollation(CatalogTransaction transaction,
                                                                    CreateCollationInfo &info) {
	throw BinderException("Iceberg databases do not support creating collations");
}

void ProtoIcebergSchemaEntry::Alter(CatalogTransaction transaction, AlterInfo &info) {
	throw NotImplementedException("ALTER is not supported in a read-only Iceberg catalog");
}

void ProtoIcebergSchemaEntry::Scan(ClientContext &context, CatalogType type,
                                   const std::function<void(CatalogEntry &)> &callback) {
	if (type != CatalogType::TABLE_ENTRY) {
		return;
	}
	if (IsNamespaceMissing()) {
		return;
	}

	auto &ic_catalog = GetIcebergCatalog();
	auto tables = tables_.Lock();

	// List once per transaction, storing placeholders so that listing doesn't load every table.
	if (!tables->Listed()) {
		DUCKDB_LOG_DEBUG(context, "proto_iceberg: ListTables in namespace '%s'", name);
		auto result = (*ic_catalog.LockCatalog())->ListTables(conversion::GetNamespace(name));
		if (!result.has_value() && result.error().kind == iceberg::ErrorKind::kNoSuchNamespace) {
			MarkNamespaceMissing();
			return;
		}
		const auto &children = UnwrapOrThrow(result, "Failed to list tables in namespace '%s'", name);

		tables->MarkListed();
		for (const auto &[_, tbl_name] : children) {
			if (std::holds_alternative<TableIndex::LookupResult::Unknown>(tables->Lookup(tbl_name))) {
				auto create_info = make_uniq<CreateTableInfo>();
				create_info->table = tbl_name;
				// Placeholders get a single UNKNOWN column, as in duckdb-iceberg (duckdb/duckdb-iceberg#515).
				create_info->columns.AddColumn(ColumnDefinition("__", LogicalType::UNKNOWN));
				tables->Store(make_uniq<ProtoIcebergTableEntry>(ic_catalog, *this, *create_info, nullptr));
			}
		}
	}

	tables->ForEach([&](const string &, reference<ProtoIcebergTableEntry> entry) { callback(entry.get()); });
}

void ProtoIcebergSchemaEntry::Scan(CatalogType type, const std::function<void(CatalogEntry &)> &callback) {
	throw NotImplementedException("Scan without context is not supported for Iceberg catalogs");
}

void ProtoIcebergSchemaEntry::DropEntry(ClientContext &context, DropInfo &info) {
	throw NotImplementedException("DROP is not supported in a read-only Iceberg catalog");
}

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::LookupEntry(CatalogTransaction transaction,
                                                                const EntryLookupInfo &lookup_info) {
	if (lookup_info.GetCatalogType() != CatalogType::TABLE_ENTRY) {
		return nullptr;
	}

	const auto &table_name = lookup_info.GetEntryName();
	auto &ic_catalog = GetIcebergCatalog();
	auto &txn = ProtoIcebergTransaction::Get(transaction.GetContext(), ic_catalog.GetAttached());
	auto tables = tables_.Lock();
	auto status = tables->Lookup(table_name);

	return std::visit(
	    Overloaded {
	        // Known absent: a negative cache, so DuckDB's fallback to other sources doesn't re-query the catalog.
	        [](const TableIndex::LookupResult::Negative &) -> optional_ptr<CatalogEntry> { return nullptr; },

	        // Stored, but a listing placeholder must be loaded first.
	        [&](const TableIndex::LookupResult::Positive &positive) -> optional_ptr<CatalogEntry> {
		        auto &entry = positive.value->get();
		        if (entry.NeedsLoad()) {
			        return LoadTableEntry(txn, table_name, transaction.GetContext(), tables);
		        }
		        return &entry;
	        },

	        // Not yet looked up: load it from the catalog.
	        [&](const TableIndex::LookupResult::Unknown &) -> optional_ptr<CatalogEntry> {
		        return LoadTableEntry(txn, table_name, transaction.GetContext(), tables);
	        },
	    },
	    status);
}

} // namespace duckdb
