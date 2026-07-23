#include "proto_iceberg_multi_file_list.hpp"
#include "adapters.hpp"
#include "unwrap.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/logging/logger.hpp"
#include "duckdb/optimizer/filter_combiner.hpp"

#include "iceberg/table.h"
#include "iceberg/table_scan.h"
#include "iceberg/manifest/manifest_entry.h"
#include "iceberg/expression/expressions.h"
#include "iceberg/file_format.h"

#include <ranges>

namespace duckdb {

namespace {

const string kFileSizeKey = "file_size";
const string kValidateExternalFileCacheKey = "validate_external_file_cache";

OpenFileInfo MakeOpenFileInfo(const iceberg::DataFile &df) {
	// Iceberg data files are immutable; skip per-file HEAD revalidation.
	unordered_map<string, Value> options = {
	    {kFileSizeKey, Value::UBIGINT(static_cast<uint64_t>(df.file_size_in_bytes))},
	    {kValidateExternalFileCacheKey, Value::BOOLEAN(false)},
	    {"etag", Value("")},
	    {"last_modified", Value::TIMESTAMP(timestamp_t(0))},
	};

	OpenFileInfo info(df.file_path);
	info.extended_info = make_shared_ptr<ExtendedOpenFileInfo>(std::move(options));
	return info;
}

std::optional<TableFilterSet> BuildTableFilterSet(ClientContext &context, const MultiFilePushdownInfo &info,
                                                  const vector<unique_ptr<Expression>> &filters) {
	if (filters.empty()) {
		return std::nullopt;
	}

	FilterCombiner combiner(context);
	for (auto &f : filters) {
		combiner.AddFilter(f->Copy());
	}

	vector<FilterPushdownResult> unused;
	auto filter_set = combiner.GenerateTableScanFilters(info.column_indexes, unused);
	return filter_set.filters.empty() ? std::nullopt : std::optional(std::move(filter_set));
}

} // namespace

ProtoIcebergMultiFileList::ProtoIcebergMultiFileList(shared_ptr<ProtoIcebergScanInfo> scan_info_p,
                                                     ClientContext &context_p, TableFilterSet table_filters_p)
    : scan_info_(std::move(scan_info_p)), table_filters_(std::move(table_filters_p)), context_(context_p) {
}

const std::shared_ptr<iceberg::Schema> &ProtoIcebergMultiFileList::GetScanSchema() const {
	return scan_info_->schema;
}

iceberg::Result<ProtoIcebergScanPlan> ProtoIcebergMultiFileList::PlanFilesImpl(const ProtoIcebergScanInfo &info,
                                                                               const TableFilterSet &filters,
                                                                               const ClientContext &context) {
	using iceberg::Error;

	ICEBERG_ASSIGN_OR_RAISE(auto scan_builder, info.table->NewScan());
	if (auto *pinned = std::get_if<PinnedRead>(&info.mode)) {
		scan_builder->UseSnapshot(pinned->snapshot_id);
	}
	std::shared_ptr<iceberg::Expression> filter{};
	if (!filters.filters.empty()) {
		filter = adapters::TranslateOrWidenFilters(filters, *info.schema);
	}
	if (filter) {
		scan_builder->Filter(filter);
	}

	ICEBERG_ASSIGN_OR_RAISE(auto scan, scan_builder->Build());
	ICEBERG_ASSIGN_OR_RAISE(auto tasks, scan->PlanFiles());

	ProtoIcebergScanPlan plan{};
	for (const auto &task : tasks) {
		if (!task->delete_files().empty()) {
			return iceberg::NotImplemented("Reading delete files is not yet supported");
		}

		if (task->data_file()->file_format != iceberg::FileFormatType::kParquet) {
			return iceberg::NotImplemented("Only Parquet data files are supported; table '{}' contains '{}' files",
			                               info.table->name().ToString(),
			                               string(iceberg::ToString(task->data_file()->file_format)));
		}

		plan.total_record_count += static_cast<idx_t>(task->data_file()->record_count);
		plan.data_files.push_back(task->data_file());
	}

	if (Logger::Get(context).ShouldLog(DefaultLogType::NAME, LogLevel::LOG_DEBUG)) {
		DUCKDB_LOG_DEBUG(context, "proto_iceberg: planned %zu data file(s) for '%s' with filter: %s",
		                 plan.data_files.size(), info.table->name().ToString(), filter ? filter->ToString() : "{}");
	}

	return plan;
}

const ProtoIcebergScanPlan &ProtoIcebergMultiFileList::PlanFiles() const {
	auto result = plan_.Get(*scan_info_, table_filters_, context_);
	// Surface a not-implemented-error (from either iceberg-cpp or us) as a more descriptive NotImplementedException.
	if (!result.has_value() && result.error().kind == iceberg::ErrorKind::kNotImplemented) {
		throw NotImplementedException(result.error().message);
	}
	return UnwrapOrThrow(result, "Failed to plan files for table '%s'", scan_info_->table->name().ToString()).get();
}

vector<OpenFileInfo> ProtoIcebergMultiFileList::GetAllFiles() const {
	return PlanFiles().data_files | std::views::transform([](const auto &df) { return MakeOpenFileInfo(*df); }) |
	       std::ranges::to<vector<OpenFileInfo>>();
}

OpenFileInfo ProtoIcebergMultiFileList::GetFile(idx_t idx) const {
	auto &[data_files, _] = PlanFiles();
	if (idx >= data_files.size()) {
		return {};
	}
	return MakeOpenFileInfo(*data_files[idx]);
}

FileExpandResult ProtoIcebergMultiFileList::GetExpandResult() const {
	return FileExpandResult::MULTIPLE_FILES;
}

idx_t ProtoIcebergMultiFileList::GetTotalFileCount() const {
	return PlanFiles().data_files.size();
}

unique_ptr<NodeStatistics> ProtoIcebergMultiFileList::GetCardinality(ClientContext &) const {
	auto &[_, total_record_count] = PlanFiles();
	// Report the total record count across all data files as both the estimate and maximum cardinality.
	return make_uniq<NodeStatistics>(total_record_count, total_record_count);
}

unique_ptr<ProtoIcebergMultiFileList>
ProtoIcebergMultiFileList::CreateFilteredList(const TableFilterSet &new_filters) const {
	TableFilterSet combined{};
	auto append = [&combined](const TableFilterSet &src) {
		for (auto &[col_id, f] : src.filters) {
			combined.PushFilter(ColumnIndex(col_id), f->Copy());
		}
	};
	append(table_filters_);
	append(new_filters);
	return make_uniq<ProtoIcebergMultiFileList>(scan_info_, context_, std::move(combined));
}

unique_ptr<MultiFileList>
ProtoIcebergMultiFileList::ComplexFilterPushdown(ClientContext &, const MultiFileOptions &options,
                                                 MultiFilePushdownInfo &info,
                                                 vector<unique_ptr<Expression>> &filters) const {
	auto filter_set = BuildTableFilterSet(context_, info, filters);
	if (!filter_set) {
		return nullptr;
	}

	DUCKDB_LOG_DEBUG(context_, "proto_iceberg: ComplexFilterPushdown applied %zu filter(s)", filter_set->filters.size());
	return CreateFilteredList(*filter_set);
}

unique_ptr<MultiFileList>
ProtoIcebergMultiFileList::DynamicFilterPushdown(ClientContext &, const MultiFileOptions &options,
                                                 const vector<string> &names, const vector<LogicalType> &types,
                                                 const vector<column_t> &column_ids, TableFilterSet &filters) const {
	if (filters.filters.empty()) {
		return nullptr;
	}

	// Skip filters already pushed down.
	TableFilterSet new_filters{};
	for (auto &[filter_idx, table_filter] : filters.filters) {
		if (filter_idx >= column_ids.size()) {
			continue;
		}
		auto column_id = column_ids[filter_idx];
		if (IsVirtualColumn(column_id)) {
			continue;
		}
		if (auto it = table_filters_.filters.find(column_id);
		    it != table_filters_.filters.end() && table_filter->Equals(*it->second)) {
			continue;
		}
		new_filters.PushFilter(ColumnIndex(column_id), table_filter->Copy());
	}

	if (new_filters.filters.empty()) {
		DUCKDB_LOG_DEBUG(context_, "proto_iceberg: DynamicFilterPushdown skipped (all filters already pushed)");
		return nullptr;
	}
	DUCKDB_LOG_DEBUG(context_, "proto_iceberg: DynamicFilterPushdown applied %zu new filter(s)",
	                 new_filters.filters.size());
	return CreateFilteredList(new_filters);
}

} // namespace duckdb
