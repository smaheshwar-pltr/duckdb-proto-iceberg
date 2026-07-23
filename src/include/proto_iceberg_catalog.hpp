#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "duckdb/storage/storage_extension.hpp"

#include "iceberg/catalog/rest/rest_catalog.h"

namespace duckdb {

class ProtoIcebergSchemaEntry;

class ProtoIcebergCatalog : public Catalog {
public:
	ProtoIcebergCatalog(AttachedDatabase &db_p, string catalog_uri,
	                    std::shared_ptr<iceberg::rest::RestCatalog> rest_catalog, string default_schema);

	static unique_ptr<Catalog> Attach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
	                                  AttachedDatabase &db, const string &name, AttachInfo &info,
	                                  AttachOptions &options);
	bool HasConflictingAttachOptions(const string &, const AttachOptions &) override {
		// TODO: Implement conflict detection
		return true;
	}

	const string &GetCatalogURI() const {
		return catalog_uri_;
	}

	iceberg::rest::RestCatalog &GetRestCatalog() const {
		return *rest_catalog_;
	}

	/// Registers a finalizer that cleans up Arrow's S3 subsystem. Must be called after Arrow S3 has been initialized.
	static void RegisterS3Finalizer();

	void Initialize(bool load_builtin) override;

	string GetCatalogType() override {
		return "iceberg";
	}

	optional_ptr<CatalogEntry> CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) override;
	void ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) override;
	optional_ptr<SchemaCatalogEntry> LookupSchema(CatalogTransaction transaction, const EntryLookupInfo &schema_lookup,
	                                              OnEntryNotFound if_not_found) override;

	string GetDefaultSchema() const override {
		return default_schema_;
	}
	bool CheckAmbiguousCatalogOrSchema(ClientContext &context, const string &name) override {
		return false;
	}

	CatalogLookupBehavior CatalogTypeLookupRule(CatalogType type) const override {
		switch (type) {
		case CatalogType::TABLE_ENTRY:
			return CatalogLookupBehavior::STANDARD;
		default:
			return CatalogLookupBehavior::NEVER_LOOKUP;
		}
	}

	PhysicalOperator &PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner, LogicalCreateTable &op,
	                                    PhysicalOperator &plan) override;
	PhysicalOperator &PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
	                             optional_ptr<PhysicalOperator> plan) override;
	PhysicalOperator &PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
	                             PhysicalOperator &plan) override;
	PhysicalOperator &PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
	                             PhysicalOperator &plan) override;
	unique_ptr<LogicalOperator> BindCreateIndex(Binder &binder, CreateStatement &stmt, TableCatalogEntry &table,
	                                            unique_ptr<LogicalOperator> plan) override;

	DatabaseSize GetDatabaseSize(ClientContext &context) override;
	bool InMemory() override {
		return false;
	}
	string GetDBPath() override {
		return catalog_uri_;
	}

private:
	void DropSchema(ClientContext &context, DropInfo &info) override;
	unique_ptr<ProtoIcebergSchemaEntry> MakeSchemaEntry(const string &name);

	string catalog_uri_;
	string default_schema_;
	std::shared_ptr<iceberg::rest::RestCatalog> rest_catalog_;
};

} // namespace duckdb
