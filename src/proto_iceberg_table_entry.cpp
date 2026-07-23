#include "constants.hpp"
#include "proto_iceberg_table_entry.hpp"
#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_transaction.hpp"
#include "proto_iceberg_scan_info.hpp"
#include "proto_iceberg_multi_file_reader.hpp"

#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/common/string_util.hpp"

#include "iceberg/table.h"
#include "iceberg/table_properties.h"
#include "iceberg/file_io.h"

namespace duckdb {
namespace {

const string kParquetScan = "parquet_scan";
const string kTableScanName = "proto_iceberg_table_scan";
const string kParquetExtension = "parquet";

namespace s3 = constants::s3;

CreateSecretInput MakeBaseS3SecretInput() {
	return {.type = s3::kSecretType,
	        .provider = s3::kProvider,
	        .storage_type = "memory",
	        .on_conflict = OnCreateConflict::REPLACE_ON_CONFLICT,
	        .persist_type = SecretPersistType::TEMPORARY};
}

string GenerateScopedSecretName(const string &catalog_name, const iceberg::TableIdentifier &table_id,
                                transaction_t txn_id) {
	// Single-level namespaces only (multi-level are skipped at listing), so levels[0] is unambiguous.
	D_ASSERT(!table_id.ns.levels.empty());
	return StringUtil::Format("__proto_ic_%s_%s_%s_%s", catalog_name, table_id.ns.levels[0], table_id.name,
	                          std::to_string(txn_id));
}

CreateSecretInput BuildScopedS3Secret(const string &catalog_name, const iceberg::Table &table, transaction_t txn_id) {
	const auto &io = table.io();
	if (!io) {
		throw IOException("Table '%s' has no FileIO; cannot vend S3 credentials", table.name().ToString());
	}

	auto scope_with_slash = [](string p) {
		if (!p.empty() && p.back() != '/') {
			p += '/';
		}
		return p;
	};
	string scope_prefix = scope_with_slash(string {table.location()});
	if (scope_prefix.empty()) {
		throw IOException("Cannot create scoped S3 secret for table '%s': empty location", table.name().ToString());
	}

	auto input = MakeBaseS3SecretInput();
	input.name = GenerateScopedSecretName(catalog_name, table.name(), txn_id);
	input.scope.push_back(std::move(scope_prefix));
	// Scope to write.data.path too, that may live outside the table's location
	if (string write_data_path {table.properties().Get(iceberg::TableProperties::kWriteDataLocation)};
	    !write_data_path.empty()) {
		input.scope.push_back(scope_with_slash(std::move(write_data_path)));
	}

	const auto &properties = io->properties();
	// TODO: Respect storage credentials REST field, not just the credentials in IO properties
	auto get_config = [&properties](const std::string_view key) -> string {
		if (auto it = properties.find(string(key)); it != properties.end()) {
			return it->second;
		}
		return {};
	};

	for (const auto &[duckdb_key, iceberg_key, kind] : s3::kPropertyMappings) {
		switch (kind) {
		case s3::PropertyKind::kPlain:
			if (auto value = get_config(iceberg_key); !value.empty()) {
				input.options[string(duckdb_key)] = Value(std::move(value));
			}
			break;
		case s3::PropertyKind::kPathStyle:
			if (get_config(iceberg_key) == "true") {
				input.options[string(duckdb_key)] = Value(s3::kUrlStylePath);
			}
			break;
		case s3::PropertyKind::kSsl:
			if (get_config(iceberg_key) == "false") {
				input.options[string(duckdb_key)] = Value::BOOLEAN(false);
			}
			break;
		}
	}

	return input;
}

void CreateScopedS3Secret(ClientContext &context, ProtoIcebergTransaction &txn, const string &catalog_name,
                          const std::shared_ptr<iceberg::Table> &table) {
	auto input = BuildScopedS3Secret(catalog_name, *table, MetaTransaction::Get(context).global_transaction_id);
	// N.B. We pin the Table object during a transaction; we therefore need not recreate a table's secrets.
	// TODO: Support credential refresh (in some manner) within a transaction, which would change this.
	if (txn.HasTrackedSecret(input.name)) {
		return;
	}

	if (!SecretManager::Get(context).CreateSecret(context, input)) {
		throw IOException("Failed to create scoped S3 secret '%s' for table '%s'", input.name,
		                  table->name().ToString());
	}
	txn.TrackSecret(input.name);
}

TableFunction GetParquetScanFunction(ClientContext &context) {
	auto &database = DatabaseInstance::GetDatabase(context);
	ExtensionHelper::AutoLoadExtension(database, kParquetExtension);

	auto &catalog_schema = Catalog::GetSystemCatalog(database).GetSchema(
	    CatalogTransaction::GetSystemTransaction(database), DEFAULT_SCHEMA);
	auto catalog_entry = catalog_schema.GetEntry(CatalogTransaction::GetSystemTransaction(database),
	                                             CatalogType::TABLE_FUNCTION_ENTRY, kParquetScan);
	if (!catalog_entry) {
		throw InvalidInputException("Function '%s' not found - parquet extension could not be loaded", kParquetScan);
	}

	auto &func_set = catalog_entry->Cast<TableFunctionCatalogEntry>();
	return func_set.functions.GetFunctionByArguments(context, {LogicalType::LIST(LogicalType::VARCHAR)});
}

TableFunction ConfigureIcebergScan(ClientContext &context, const shared_ptr<ProtoIcebergScanInfo> &scan_info) {
	auto iceberg_scan = GetParquetScanFunction(context);
	iceberg_scan.get_multi_file_reader = ProtoIcebergMultiFileReader::CreateInstance;
	iceberg_scan.function_info = scan_info;
	iceberg_scan.name = kTableScanName;
	iceberg_scan.projection_pushdown = true;
	return iceberg_scan;
}

/// Builds a placeholder input that parquet_scan's binder expects; the real file list is derived lazily by
/// ProtoIcebergMultiFileList via iceberg-cpp scan planning.
vector<Value> MakePlaceholderScanInputs() {
	return {Value::LIST(LogicalType::VARCHAR, vector<Value> {Value("iceberg_placeholder")})};
}

pair<TableFunction, unique_ptr<FunctionData>> BindIcebergScan(ClientContext &context, TableFunction iceberg_scan) {
	auto inputs = MakePlaceholderScanInputs();
	named_parameter_map_t named_params;
	vector<LogicalType> input_table_types;
	vector<string> input_table_names;
	TableFunctionRef empty_ref;
	TableFunctionBindInput bind_input(inputs, named_params, input_table_types, input_table_names,
	                                  iceberg_scan.function_info.get(), nullptr, iceberg_scan, empty_ref);

	vector<LogicalType> return_types;
	vector<string> return_names;
	auto bind_data = iceberg_scan.bind(context, bind_input, return_types, return_names);

	return {std::move(iceberg_scan), std::move(bind_data)};
}

} // namespace

ProtoIcebergTableEntry::ProtoIcebergTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info,
                                               shared_ptr<ProtoIcebergScanInfo> scan_info_p)
    : TableCatalogEntry(catalog, schema, info), scan_info_(std::move(scan_info_p)) {
}

ProtoIcebergTableEntry::~ProtoIcebergTableEntry() = default;

unique_ptr<BaseStatistics> ProtoIcebergTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	return nullptr;
}

TableFunction ProtoIcebergTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	D_ASSERT(scan_info_ && scan_info_->table);

	auto &ic_catalog = catalog.Cast<ProtoIcebergCatalog>();
	auto &txn = ProtoIcebergTransaction::Get(context, ic_catalog.GetAttached());

	CreateScopedS3Secret(context, txn, ic_catalog.GetCatalogURI(), scan_info_->table);

	auto iceberg_scan = ConfigureIcebergScan(context, scan_info_);
	auto [scan, scan_bind_data] = BindIcebergScan(context, std::move(iceberg_scan));
	bind_data = std::move(scan_bind_data);
	return scan;
}

TableStorageInfo ProtoIcebergTableEntry::GetStorageInfo(ClientContext &context) {
	return {};
}

void ProtoIcebergTableEntry::BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj,
                                                   LogicalUpdate &update, ClientContext &context) {
	throw NotImplementedException("UPDATE is not supported in a read-only Iceberg catalog");
}

} // namespace duckdb
