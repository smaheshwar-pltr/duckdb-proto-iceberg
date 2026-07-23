#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"

namespace duckdb {

class ProtoIcebergCatalog;
class ProtoIcebergSchemaEntry;
class ProtoIcebergTransaction;
struct ProtoIcebergScanInfo;

class ProtoIcebergTableEntry : public TableCatalogEntry {
public:
	ProtoIcebergTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
	                       shared_ptr<ProtoIcebergScanInfo> scan_info_p);
	~ProtoIcebergTableEntry() override;

	/// Returns true if this is a lightweight entry from table listing
	/// that needs a full LoadTable before it can be scanned or described.
	bool NeedsLoad() const {
		return !scan_info_;
	}

	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;
	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;

	TableStorageInfo GetStorageInfo(ClientContext &context) override;
	void BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj, LogicalUpdate &update,
	                           ClientContext &context) override;

private:
	shared_ptr<ProtoIcebergScanInfo> scan_info_;
};

} // namespace duckdb
