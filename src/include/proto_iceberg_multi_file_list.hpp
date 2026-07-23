#pragma once

#include "proto_iceberg_scan_info.hpp"

#include "duckdb/common/multi_file/multi_file_list.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/storage/statistics/node_statistics.hpp"
#include "iceberg/util/lazy.h"

#include <memory>
#include <string>

namespace iceberg {
class Schema;
class DataFile;
} // namespace iceberg

namespace duckdb {

struct ProtoIcebergScanPlan {
	vector<std::shared_ptr<iceberg::DataFile>> data_files {};
	idx_t total_record_count = 0;
};

class ProtoIcebergMultiFileList : public MultiFileList {
public:
	ProtoIcebergMultiFileList(shared_ptr<ProtoIcebergScanInfo> scan_info_p, ClientContext &context_p,
	                          TableFilterSet table_filters_p = {});

	vector<OpenFileInfo> GetAllFiles() const override;
	FileExpandResult GetExpandResult() const override;
	idx_t GetTotalFileCount() const override;
	unique_ptr<NodeStatistics> GetCardinality(ClientContext &context) const override;
	unique_ptr<MultiFileList> ComplexFilterPushdown(ClientContext &context, const MultiFileOptions &options,
	                                                MultiFilePushdownInfo &info,
	                                                vector<unique_ptr<Expression>> &filters) const override;
	unique_ptr<MultiFileList> DynamicFilterPushdown(ClientContext &context, const MultiFileOptions &options,
	                                                const vector<string> &names, const vector<LogicalType> &types,
	                                                const vector<column_t> &column_ids,
	                                                TableFilterSet &filters) const override;

	/// Returns the resolved scan schema (the pinned snapshot's schema for a pinned read, else the current schema).
	const std::shared_ptr<iceberg::Schema> &GetScanSchema() const;

protected:
	OpenFileInfo GetFile(idx_t idx) const override;

private:
	unique_ptr<ProtoIcebergMultiFileList> CreateFilteredList(const TableFilterSet &new_filters) const;

	const ProtoIcebergScanPlan &PlanFiles() const;
	static iceberg::Result<ProtoIcebergScanPlan>
	PlanFilesImpl(const ProtoIcebergScanInfo &info, const TableFilterSet &filters, const ClientContext &context);
	iceberg::Lazy<PlanFilesImpl> plan_;

	shared_ptr<ProtoIcebergScanInfo> scan_info_;
	TableFilterSet table_filters_;
	ClientContext &context_;
};

} // namespace duckdb
