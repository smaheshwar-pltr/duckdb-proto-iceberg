#include "proto_iceberg_multi_file_list.hpp"
#include "conversion.hpp"
#include "unwrap.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/multi_file/multi_file_reader.hpp"
#include "duckdb/logging/logger.hpp"
#include "duckdb/optimizer/filter_combiner.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"

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
	return filter_set.HasFilters() ? std::optional(std::move(filter_set)) : std::nullopt;
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
	std::shared_ptr<iceberg::Expression> filter {};
	if (filters.HasFilters()) {
		filter = conversion::TranslateOrWidenFilters(filters, *info.schema);
	}
	if (filter) {
		scan_builder->Filter(filter);
	}

	ICEBERG_ASSIGN_OR_RAISE(auto scan, scan_builder->Build());
	ICEBERG_ASSIGN_OR_RAISE(auto tasks, scan->PlanFiles());

	ProtoIcebergScanPlan plan {};
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
	auto combined = table_filters_.Copy();
	auto append = [&combined](const TableFilterSet &src) {
		for (auto &entry : src) {
			auto &filter =
			    ExpressionFilter::GetExpressionFilter(entry.Filter(), "ProtoIcebergMultiFileList::CreateFilteredList");
			combined->PushFilter(entry.GetIndex(), filter.Copy());
		}
	};
	append(new_filters);
	return make_uniq<ProtoIcebergMultiFileList>(scan_info_, context_, std::move(*combined));
}

unique_ptr<MultiFileList>
ProtoIcebergMultiFileList::ComplexFilterPushdown(ClientContext &, const MultiFileOptions &options,
                                                 MultiFilePushdownInfo &info,
                                                 vector<unique_ptr<Expression>> &filters) const {
	auto filter_set = BuildTableFilterSet(context_, info, filters);
	if (!filter_set) {
		return nullptr;
	}

	TableFilterSet remapped_filters;
	for (auto &entry : *filter_set) {
		auto filter_index = entry.GetIndex().GetIndex();
		if (filter_index >= info.column_indexes.size()) {
			continue;
		}
		auto &column_index = info.column_indexes[filter_index];
		if (column_index.HasChildren()) {
			continue;
		}
		auto column_id = column_index.GetPrimaryIndex();
		if (IsVirtualColumn(column_id)) {
			continue;
		}
		auto &filter =
		    ExpressionFilter::GetExpressionFilter(entry.Filter(), "ProtoIcebergMultiFileList::ComplexFilterPushdown");
		remapped_filters.PushFilter(ProjectionIndex(column_id), filter.Copy());
	}
	if (!remapped_filters.HasFilters()) {
		return nullptr;
	}
	DUCKDB_LOG_DEBUG(context_, "proto_iceberg: ComplexFilterPushdown applied %zu filter(s)",
	                 remapped_filters.FilterCount());
	return CreateFilteredList(remapped_filters);
}

unique_ptr<MultiFileList>
ProtoIcebergMultiFileList::DynamicFilterPushdown(MultiFileDynamicPushdownInfo &pushdown_info) const {
	auto &filters = pushdown_info.filters;
	auto &column_indexes = pushdown_info.column_indexes;
	if (!filters.HasFilters()) {
		return nullptr;
	}

	// Skip filters already pushed down.
	TableFilterSet new_filters {};
	for (auto &entry : filters) {
		auto filter_idx = entry.GetIndex().GetIndex();
		if (filter_idx >= column_indexes.size()) {
			continue;
		}
		auto &column_index = column_indexes[filter_idx];
		if (column_index.HasChildren()) {
			continue;
		}
		auto column_id = column_index.GetPrimaryIndex();
		if (IsVirtualColumn(column_id)) {
			continue;
		}
		auto &table_filter =
		    ExpressionFilter::GetExpressionFilter(entry.Filter(), "ProtoIcebergMultiFileList::DynamicFilterPushdown");
		auto existing = table_filters_.TryGetFilterByColumnIndex(ProjectionIndex(column_id));
		if (existing && table_filter.Equals(ExpressionFilter::GetExpressionFilter(
		                    *existing, "ProtoIcebergMultiFileList::DynamicFilterPushdown"))) {
			continue;
		}
		new_filters.PushFilter(ProjectionIndex(column_id), table_filter.Copy());
	}

	if (!new_filters.HasFilters()) {
		DUCKDB_LOG_DEBUG(context_, "proto_iceberg: DynamicFilterPushdown skipped (all filters already pushed)");
		return nullptr;
	}
	DUCKDB_LOG_DEBUG(context_, "proto_iceberg: DynamicFilterPushdown applied %zu new filter(s)",
	                 new_filters.FilterCount());
	return CreateFilteredList(new_filters);
}

} // namespace duckdb
