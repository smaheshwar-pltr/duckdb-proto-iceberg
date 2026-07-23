#include "adapters.hpp"
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
		ColumnDefinition col(string(field.name()), adapters::MapIcebergType(*field.type()));
		if (!field.doc().empty()) {
			col.SetComment(Value(string(field.doc())));
		}
		create_info->columns.AddColumn(std::move(col));
	}
	return make_uniq<ProtoIcebergTableEntry>(ic_catalog, schema, *create_info, std::move(scan_info));
}

/// Resolves the scan mode, snapshot, and schema for a table at the transaction's point-in-time.
/// Always returns a populated scan info, or throws TransactionException if no state is reachable as of txn start.
shared_ptr<ProtoIcebergScanInfo> ResolveScanInfo(ClientContext &context, ProtoIcebergTransaction &txn,
                                                 std::shared_ptr<iceberg::Table> table, const string &namespace_name,
                                                 const string &table_name) {
	// If the table has snapshots and was updated since our transaction started, pin to the txn-time snapshot (and to
	// that snapshot's schema, though that might not have been the current schema at txn-time due to schema evolution).
	if (auto txn_start = iceberg::TimePointMsFromUnixMs(txn.GetStartTimestampMs());
	    !table->snapshots().empty() && table->last_updated_ms() > txn_start) {
		auto maybe_snapshot_id = iceberg::SnapshotUtil::OptionalSnapshotIdAsOfTime(*table, txn_start);
		if (!maybe_snapshot_id.has_value()) {
			// Table exists but has no snapshot reachable as of txn start. This can happen if the table was created
			// after txn start, or even if the table was present at txn start but only gained snapshots after.
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
		return make_shared_ptr<ProtoIcebergScanInfo>(std::move(table), PinnedRead {snapshot_id}, std::move(schema));
	}

	// Snapshot-less, or unchanged since txn start — both read the current snapshot and schema. For a snapshot-less
	// table, this holds even if its schema changed since txn start, since the original schema isn't easily recoverable.
	auto schema = UnwrapOrThrow(table->schema(), "Failed to read schema for table '%s.%s'", namespace_name, table_name);
	return make_shared_ptr<ProtoIcebergScanInfo>(std::move(table), CurrentRead {}, std::move(schema));
}

} // namespace

optional_ptr<CatalogEntry> ProtoIcebergSchemaEntry::LoadFullTableEntry(ProtoIcebergTransaction &txn,
                                                                       const string &table_name, ClientContext &context,
                                                                       Mutex<TableState>::Guard &tables) {
	auto &ic_catalog = GetIcebergCatalog();
	DUCKDB_LOG_DEBUG(context, "proto_iceberg: LoadTable '%s.%s'", name, table_name);
	auto table_result = ic_catalog.GetRestCatalog().LoadTable(adapters::GetTableIdentifier(name, table_name));

	// Return no entry on LoadTable NotFound exceptions; surface any other error as an exception.
	if (!table_result.has_value()) {
		const auto &[kind, message] = table_result.error();
		if (bool namespace_missing = kind == iceberg::ErrorKind::kNoSuchNamespace;
		    namespace_missing || kind == iceberg::ErrorKind::kNoSuchTable) {
			if (namespace_missing) {
				// Deferred namespace existence validation means we can reach table load without a present namespace.
				MarkNamespaceNotFound();
			}
			tables->store.PutMiss(table_name);
			return nullptr;
		}
		throw IOException("Failed to load table '%s.%s': %s", name, table_name, message);
	}

	// LoadTable above initialized Arrow S3, so we now register S3 cleanup.
	ProtoIcebergCatalog::RegisterS3Finalizer();

	auto scan_info = ResolveScanInfo(context, txn, std::move(table_result.value()), name, table_name);
	auto table_entry = CreateTableEntry(ic_catalog, *this, std::move(scan_info), table_name);
	return tables->store.Put(table_name, std::move(table_entry)).get();
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
	if (NamespaceNotFound()) {
		// A prior LoadTable proved this namespace absent; nothing to list.
		return;
	}

	auto &ic_catalog = GetIcebergCatalog();
	auto tables = tables_.Lock();

	// List tables at most once per namespace per transaction. Creates lightweight entries to avoid calling LoadTable
	// for every table during SHOW ALL TABLES; LoadTable is deferred to LookupEntry.
	if (!tables->listed) {
		DUCKDB_LOG_DEBUG(context, "proto_iceberg: ListTables in namespace '%s'", name);
		auto result = ic_catalog.GetRestCatalog().ListTables(adapters::GetNamespace(name));
		if (!result.has_value() && result.error().kind == iceberg::ErrorKind::kNoSuchNamespace) {
			// An optimistically-created schema that doesn't exist; nothing to list.
			MarkNamespaceNotFound();
			return;
		}
		const auto &children = UnwrapOrThrow(result, "Failed to list tables in namespace '%s'", name);

		tables->listed = true;
		for (const auto &[_, tbl_name] : children) {
			if (auto status = tables->store.Lookup(tbl_name); std::holds_alternative<TableStore::Unknown>(status)) {
				auto create_info = make_uniq<CreateTableInfo>();
				create_info->table = tbl_name;
				// N.B. "__"/UNKNOWN placeholder for not-yet-resolved tables to avoid a LoadTable per listed table, see
				// https://github.com/duckdb/duckdb-iceberg/issues/515.
				create_info->columns.AddColumn(ColumnDefinition("__", LogicalType::UNKNOWN));
				tables->store.Put(tbl_name,
				                  make_uniq<ProtoIcebergTableEntry>(ic_catalog, *this, *create_info, nullptr));
			}
		}
	}

	tables->store.ForEachPositive(
	    [&](const string &, const unique_ptr<ProtoIcebergTableEntry> &entry) { callback(*entry); });
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
	auto status = tables->store.Lookup(table_name);

	return std::visit(
	    Overloaded {
	        // If table has been confirmed absent from catalog, report that immediately.
	        // N.B. On a table not being found in this catalog, DuckDB falls back to searching other sources.
	        // Our negative cache therefore prevents repeated, potentially costly REST catalog lookups for the
	        // same missing table.
	        [](TableStore::Negative &) -> optional_ptr<CatalogEntry> { return nullptr; },

	        // If the table has been stored already, return it — unless it's a lightweight entry from table listing that
	        // needs a full LoadTable to populate scan_info.
	        [&](const TableStore::Positive &positive) -> optional_ptr<CatalogEntry> {
		        auto &entry = **positive.value;
		        if (entry.NeedsLoad()) {
			        return LoadFullTableEntry(txn, table_name, transaction.GetContext(), tables);
		        }
		        return &entry;
	        },

	        // Otherwise, the table has not yet been stored; load it from the Iceberg REST catalog and store accordingly
	        // (depending on whether the table exists or not).
	        [&](TableStore::Unknown &) -> optional_ptr<CatalogEntry> {
		        return LoadFullTableEntry(txn, table_name, transaction.GetContext(), tables);
	        },
	    },
	    status);
}

} // namespace duckdb
