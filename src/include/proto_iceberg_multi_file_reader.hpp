#pragma once

#include "duckdb/common/multi_file/multi_file_reader.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

class ProtoIcebergMultiFileReader : public MultiFileReader {
public:
	explicit ProtoIcebergMultiFileReader(shared_ptr<TableFunctionInfo> function_info_p);

	static unique_ptr<MultiFileReader> CreateInstance(const TableFunction &table_function);

	shared_ptr<MultiFileList> CreateFileList(ClientContext &context, const vector<string> &paths,
	                                         const FileGlobInput &glob_input) override;

	bool Bind(MultiFileOptions &options, MultiFileList &files, vector<LogicalType> &return_types, vector<string> &names,
	          MultiFileReaderBindData &bind_data) override;

	void BindOptions(MultiFileOptions &options, MultiFileList &files, vector<LogicalType> &return_types,
	                 vector<string> &names, MultiFileReaderBindData &bind_data) override;

private:
	shared_ptr<TableFunctionInfo> function_info_;
};

} // namespace duckdb
