#pragma once

#include "signed_store.hpp"
#include "mutex.hpp"

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"

#include <atomic>
#include <memory>

namespace duckdb {

class ProtoIcebergCatalog;
class ProtoIcebergTableEntry;
class ProtoIcebergTransaction;

class ProtoIcebergSchemaEntry : public SchemaCatalogEntry {
public:
	ProtoIcebergSchemaEntry(Catalog &catalog, CreateSchemaInfo &info);
	~ProtoIcebergSchemaEntry() override;

	ProtoIcebergCatalog &GetIcebergCatalog() const;

	/// Returns whether the catalog has reported the namespace as missing. Deferred namespace validation means a schema
	/// is optimistically marked as present until a LoadTable marks it as absent.
	bool NamespaceNotFound() const {
		return namespace_not_found_.load(std::memory_order_acquire);
	}

	/// Marks this namespace as absent in the catalog (deferred validation from a failed LoadTable).
	void MarkNamespaceNotFound() {
		namespace_not_found_.store(true, std::memory_order_release);
	}

	using TableStore = SignedStore<string, unique_ptr<ProtoIcebergTableEntry>>;

	struct TableState {
		TableStore store;
		bool listed = false;
	};

	optional_ptr<CatalogEntry> CreateTable(CatalogTransaction transaction, BoundCreateTableInfo &info) override;
	optional_ptr<CatalogEntry> CreateFunction(CatalogTransaction transaction, CreateFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateIndex(CatalogTransaction transaction, CreateIndexInfo &info,
	                                       TableCatalogEntry &table) override;
	optional_ptr<CatalogEntry> CreateView(CatalogTransaction transaction, CreateViewInfo &info) override;
	optional_ptr<CatalogEntry> CreateSequence(CatalogTransaction transaction, CreateSequenceInfo &info) override;
	optional_ptr<CatalogEntry> CreateTableFunction(CatalogTransaction transaction,
	                                               CreateTableFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateCopyFunction(CatalogTransaction transaction,
	                                              CreateCopyFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreatePragmaFunction(CatalogTransaction transaction,
	                                                CreatePragmaFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateType(CatalogTransaction transaction, CreateTypeInfo &info) override;
	optional_ptr<CatalogEntry> CreateCollation(CatalogTransaction transaction, CreateCollationInfo &info) override;

	void Alter(CatalogTransaction transaction, AlterInfo &info) override;
	void Scan(ClientContext &context, CatalogType type, const std::function<void(CatalogEntry &)> &callback) override;
	void Scan(CatalogType type, const std::function<void(CatalogEntry &)> &callback) override;
	void DropEntry(ClientContext &context, DropInfo &info) override;
	optional_ptr<CatalogEntry> LookupEntry(CatalogTransaction transaction, const EntryLookupInfo &lookup_info) override;

private:
	/// Loads a table from the REST catalog, resolves its scan info, and stores in the table state.
	/// Returns a pointer to the stored entry, or nullptr if the table doesn't exist.
	optional_ptr<CatalogEntry> LoadFullTableEntry(ProtoIcebergTransaction &txn, const string &table_name,
	                                              ClientContext &context, Mutex<TableState>::Guard &tables);

	std::atomic<bool> namespace_not_found_ = false;
	Mutex<TableState> tables_;
};

} // namespace duckdb
