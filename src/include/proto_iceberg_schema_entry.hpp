#pragma once

#include "signed_store.hpp"
#include "mutex.hpp"

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/common/helper.hpp"

#include <atomic>
#include <memory>
#include <utility>

namespace duckdb {

class ProtoIcebergCatalog;
class ProtoIcebergTableEntry;
class ProtoIcebergTransaction;

class ProtoIcebergSchemaEntry : public SchemaCatalogEntry {
public:
	ProtoIcebergSchemaEntry(Catalog &catalog, CreateSchemaInfo &info);
	~ProtoIcebergSchemaEntry() override;

	ProtoIcebergCatalog &GetIcebergCatalog() const;

	/// Returns whether a listing or table load has shown that this namespace doesn't exist.
	bool IsNamespaceMissing() const {
		return namespace_missing_.load(std::memory_order_acquire);
	}

	void MarkNamespaceMissing() {
		namespace_missing_.store(true, std::memory_order_release);
	}

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
	using TableIndex = SignedStore<string, reference<ProtoIcebergTableEntry>>;

	/// Owns this schema's table entries until the transaction ends, as DuckDB may still reference replaced ones. The
	/// index names each table's current entry, or records that the table doesn't exist.
	class TableRegistry {
	public:
		bool Listed() const {
			return listed_;
		}
		void MarkListed() {
			listed_ = true;
		}

		TableIndex::LookupResult Lookup(const string &name) {
			return index_.Lookup(name);
		}

		template <typename Fn>
		void ForEach(Fn &&fn) {
			index_.ForEachPositive(std::forward<Fn>(fn));
		}

		ProtoIcebergTableEntry &Store(unique_ptr<ProtoIcebergTableEntry> entry);
		void MarkMissing(const string &name) {
			index_.PutMiss(name);
		}

	private:
		vector<unique_ptr<ProtoIcebergTableEntry>> entries_;
		TableIndex index_;
		bool listed_ = false;
	};

	/// Loads a table from the catalog and stores it, returning nullptr if the table doesn't exist.
	optional_ptr<CatalogEntry> LoadTableEntry(ProtoIcebergTransaction &txn, const string &table_name,
	                                          ClientContext &context, Mutex<TableRegistry>::Guard &tables);

	std::atomic<bool> namespace_missing_ = false;
	Mutex<TableRegistry> tables_;
};

} // namespace duckdb
