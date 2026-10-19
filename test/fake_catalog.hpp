#pragma once

#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_transaction.hpp"

#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"

#include "iceberg/arrow/arrow_io_util.h"
#include "iceberg/catalog/memory/in_memory_catalog.h"

#include <atomic>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace duckdb {

/// A test catalog that lists `table_count` tables without materializing them, so any load reports it missing.
class FakeCatalog : public iceberg::InMemoryCatalog {
public:
	explicit FakeCatalog(std::size_t table_count)
	    : InMemoryCatalog("fake", iceberg::arrow::MakeMockFileIO(), /*warehouse_location=*/"", {}),
	      table_count_(table_count) {
		// So ListNamespaces surfaces "default" to a schema scan.
		(void)CreateNamespace({.levels = {"default"}}, {});
	}

	std::atomic<std::size_t> load_table_calls = 0;

	static std::string TableName(std::size_t index) {
		return std::format("t{:04}", index);
	}

	iceberg::Result<std::vector<iceberg::TableIdentifier>> ListTables(const iceberg::Namespace &ns) const override {
		std::vector<iceberg::TableIdentifier> identifiers;
		identifiers.reserve(table_count_);
		for (std::size_t i = 0; i < table_count_; i++) {
			identifiers.push_back({.ns = ns, .name = TableName(i)});
		}
		return identifiers;
	}

	iceberg::Result<std::shared_ptr<iceberg::Table>> LoadTable(const iceberg::TableIdentifier &identifier) override {
		++load_table_calls;
		return iceberg::NoSuchTable("table '{}' does not exist", identifier.name);
	}

private:
	std::size_t table_count_;
};

namespace fake_catalog_detail {

struct StorageInfo : StorageExtensionInfo {
	std::shared_ptr<iceberg::Catalog> catalog;
};

inline unique_ptr<Catalog> Attach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &,
                                  AttachedDatabase &db, const string &, AttachInfo &, AttachOptions &) {
	return make_uniq<ProtoIcebergCatalog>(db, "fake", static_cast<StorageInfo &>(*storage_info).catalog, "default");
}

inline unique_ptr<TransactionManager> CreateTransactionManager(optional_ptr<StorageExtensionInfo>, AttachedDatabase &db,
                                                               Catalog &catalog) {
	return make_uniq<ProtoIcebergTransactionManager>(db, catalog.Cast<ProtoIcebergCatalog>());
}

class FakeStorageExtension : public StorageExtension {
public:
	explicit FakeStorageExtension(std::shared_ptr<iceberg::Catalog> catalog) {
		attach = Attach;
		create_transaction_manager = CreateTransactionManager;
		auto info = make_shared_ptr<StorageInfo>();
		info->catalog = std::move(catalog);
		storage_info = std::move(info);
	}
};

} // namespace fake_catalog_detail

/// Registers a storage extension of type `type` so ATTACH (TYPE <type>) builds a ProtoIcebergCatalog over `catalog`.
inline void RegisterFakeIcebergCatalog(DatabaseInstance &instance, const string &type,
                                       std::shared_ptr<iceberg::Catalog> catalog) {
	StorageExtension::Register(DBConfig::GetConfig(instance), type,
	                           make_shared_ptr<fake_catalog_detail::FakeStorageExtension>(std::move(catalog)));
}

} // namespace duckdb
