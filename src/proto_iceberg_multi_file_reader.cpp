#include "proto_iceberg_multi_file_reader.hpp"
#include "proto_iceberg_multi_file_list.hpp"
#include "proto_iceberg_scan_info.hpp"
#include "adapters.hpp"

#include "iceberg/schema.h"

namespace duckdb {

ProtoIcebergMultiFileReader::ProtoIcebergMultiFileReader(shared_ptr<TableFunctionInfo> function_info_p)
    : function_info_(std::move(function_info_p)) {
}

unique_ptr<MultiFileReader> ProtoIcebergMultiFileReader::CreateInstance(const TableFunction &table_function) {
	return make_uniq<ProtoIcebergMultiFileReader>(table_function.function_info);
}

shared_ptr<MultiFileList> ProtoIcebergMultiFileReader::CreateFileList(ClientContext &context,
                                                                      const vector<string> &paths,
                                                                      const FileGlobInput &glob_input) {
	D_ASSERT(function_info_);
	auto scan_info = shared_ptr_cast<TableFunctionInfo, ProtoIcebergScanInfo>(function_info_);
	return make_shared_ptr<ProtoIcebergMultiFileList>(std::move(scan_info), context);
}

bool ProtoIcebergMultiFileReader::Bind(MultiFileOptions &options, MultiFileList &files,
                                       vector<LogicalType> &return_types, vector<string> &names,
                                       MultiFileReaderBindData &bind_data) {
	// Configure bind schema with field-ID identifiers for column mapping.
	const auto &list = files.Cast<ProtoIcebergMultiFileList>();
	bind_data.schema = adapters::BuildColumnList(*list.GetScanSchema());
	for (const auto &col : bind_data.schema) {
		names.push_back(col.name);
		return_types.push_back(col.type);
	}
	bind_data.mapping = MultiFileColumnMappingMode::BY_FIELD_ID;
	return true;
}

void ProtoIcebergMultiFileReader::BindOptions(MultiFileOptions &options, MultiFileList &files,
                                              vector<LogicalType> &return_types, vector<string> &names,
                                              MultiFileReaderBindData &bind_data) {
	// Disable Hive partitioning; Iceberg handles it.
	options.auto_detect_hive_partitioning = false;
	options.hive_partitioning = false;
	options.union_by_name = false;
	MultiFileReader::BindOptions(options, files, return_types, names, bind_data);
}

} // namespace duckdb
