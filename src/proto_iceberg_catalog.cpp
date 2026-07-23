#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_schema_entry.hpp"
#include "proto_iceberg_transaction.hpp"
#include "unwrap.hpp"

#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckdb/logging/logger.hpp"

#include "iceberg/arrow/arrow_io_util.h"

namespace duckdb {

ProtoIcebergCatalog::ProtoIcebergCatalog(AttachedDatabase &db_p, string catalog_uri,
                                         std::shared_ptr<iceberg::rest::RestCatalog> rest_catalog,
                                         string default_schema)
    : Catalog(db_p), catalog_uri_(std::move(catalog_uri)), default_schema_(std::move(default_schema)),
      rest_catalog_(std::move(rest_catalog)) {
}

void ProtoIcebergCatalog::RegisterS3Finalizer() {
	// Must be called after Arrow S3 initialization so static destruction runs at the right time.
	struct S3Finalizer {
		~S3Finalizer() {
			[[maybe_unused]] auto status = iceberg::arrow::FinalizeS3();
		}
	};

	static S3Finalizer instance;
}

void ProtoIcebergCatalog::Initialize(bool load_builtin) {
}

optional_ptr<CatalogEntry> ProtoIcebergCatalog::CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) {
	throw NotImplementedException("CREATE SCHEMA is not supported in a read-only Iceberg catalog");
}

void ProtoIcebergCatalog::DropSchema(ClientContext &context, DropInfo &info) {
	throw NotImplementedException("DROP SCHEMA is not supported in a read-only Iceberg catalog");
}

unique_ptr<ProtoIcebergSchemaEntry> ProtoIcebergCatalog::MakeSchemaEntry(const string &name) {
	auto info = make_uniq<CreateSchemaInfo>();
	info->schema = name;
	return make_uniq<ProtoIcebergSchemaEntry>(*this, *info);
}

void ProtoIcebergCatalog::ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) {
	auto &txn = ProtoIcebergTransaction::Get(context, GetAttached());
	auto schemas = txn.LockSchemas();

	if (!schemas.Listed()) {
		DUCKDB_LOG_DEBUG(context, "proto_iceberg: ListNamespaces at root");
		auto namespaces = UnwrapOrThrow(GetRestCatalog().ListNamespaces({}), "Failed to list namespaces");

		schemas.MarkListed();

		for (auto &ns : namespaces) {
			if (ns.levels.size() != 1) {
				// We don't expect multi-level namespaces being children of the root namespace.
				// We choose to ignore them, and report single-level children as schemas instead.
				DUCKDB_LOG_WARNING(context, "proto_iceberg: skipping unexpected child namespace '%s'", ns.ToString());
				continue;
			}

			auto &ns_name = ns.levels[0];
			if (!schemas.Lookup(ns_name)) {
				schemas.Store(ns_name, MakeSchemaEntry(ns_name));
			}
		}
	}

	schemas.ForEach([&](ProtoIcebergSchemaEntry &entry) { callback(entry); });
}

optional_ptr<SchemaCatalogEntry> ProtoIcebergCatalog::LookupSchema(CatalogTransaction transaction,
                                                                   const EntryLookupInfo &schema_lookup,
                                                                   OnEntryNotFound if_not_found) {
	auto schema_name = schema_lookup.GetEntryName();

	// Redirect DuckDB's default schema to the configured default one.
	if (schema_name == DEFAULT_SCHEMA && GetDefaultSchema() != DEFAULT_SCHEMA) {
		return LookupSchema(transaction, EntryLookupInfo(CatalogType::SCHEMA_ENTRY, GetDefaultSchema()), if_not_found);
	}

	auto &txn = ProtoIcebergTransaction::Get(transaction.GetContext(), GetAttached());
	auto schemas = txn.LockSchemas();

	if (auto existing = schemas.Lookup(schema_name)) {
		// Report if the namespace was proven non-existent by a prior table load (deferred validation).
		if (existing->NamespaceNotFound()) {
			if (if_not_found == OnEntryNotFound::RETURN_NULL) {
				return nullptr;
			}
			throw CatalogException("Schema '%s' does not exist", schema_name);
		}
		return existing.get();
	}

	// Optimistically create schema entry without verifying namespace existence (deferred validation).
	return &schemas.Store(schema_name, MakeSchemaEntry(schema_name));
}

PhysicalOperator &ProtoIcebergCatalog::PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner,
                                                         LogicalCreateTable &op, PhysicalOperator &plan) {
	throw NotImplementedException("CREATE TABLE AS is not supported in a read-only Iceberg catalog");
}

PhysicalOperator &ProtoIcebergCatalog::PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner,
                                                  LogicalInsert &op, optional_ptr<PhysicalOperator> plan) {
	throw NotImplementedException("INSERT is not supported in a read-only Iceberg catalog");
}

PhysicalOperator &ProtoIcebergCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner,
                                                  LogicalDelete &op, PhysicalOperator &plan) {
	throw NotImplementedException("DELETE is not supported in a read-only Iceberg catalog");
}

PhysicalOperator &ProtoIcebergCatalog::PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner,
                                                  LogicalUpdate &op, PhysicalOperator &plan) {
	throw NotImplementedException("UPDATE is not supported in a read-only Iceberg catalog");
}

unique_ptr<LogicalOperator> ProtoIcebergCatalog::BindCreateIndex(Binder &binder, CreateStatement &stmt,
                                                                 TableCatalogEntry &table,
                                                                 unique_ptr<LogicalOperator> plan) {
	throw NotImplementedException("CREATE INDEX is not supported in a read-only Iceberg catalog");
}

DatabaseSize ProtoIcebergCatalog::GetDatabaseSize(ClientContext &context) {
	return {};
}

} // namespace duckdb
