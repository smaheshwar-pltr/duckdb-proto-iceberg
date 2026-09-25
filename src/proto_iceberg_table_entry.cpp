#include "constants.hpp"
#include "s3_conversion.hpp"
#include "proto_iceberg_table_entry.hpp"
#include "proto_iceberg_scan_info.hpp"
#include "proto_iceberg_multi_file_reader.hpp"

#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/logging/logger.hpp"

#include "iceberg/table.h"
#include "iceberg/table_properties.h"
#include "iceberg/file_io.h"

#include <utility>

namespace duckdb {
namespace {

using constants::kConfigProvider;
using constants::kS3SecretType;

const string kParquetScan = "parquet_scan";
const string kTableScanName = "proto_iceberg_table_scan";
const string kParquetExtension = "parquet";
const string kHttpfsExtension = "httpfs";
const string kScopedSecretsStateKey = "proto_iceberg_scoped_secrets";

CreateSecretInput MakeBaseS3SecretInput() {
	return {.type = kS3SecretType,
	        .provider = kConfigProvider,
	        .storage_type = "memory",
	        .on_conflict = OnCreateConflict::REPLACE_ON_CONFLICT,
	        .persist_type = SecretPersistType::TEMPORARY};
}

CreateSecretInput BuildScopedS3Secret(const iceberg::Table &table) {
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
	input.scope.push_back(std::move(scope_prefix));
	// Scope to write.data.path too, that may live outside the table's location
	if (string write_data_path {table.properties().Get(iceberg::TableProperties::kWriteDataLocation)};
	    !write_data_path.empty()) {
		input.scope.push_back(scope_with_slash(std::move(write_data_path)));
	}

	// TODO: Respect storage credentials REST field, not just the credentials in IO properties
	input.options = conversion::ConvertIcebergPropertiesToS3Secret(io->properties());
	return input;
}

/// A connection's temporary secrets for vended credentials, which are dropped when its transaction ends.
///
/// Secrets are written in the connection's system catalog transaction, which DuckDB may commit after the Iceberg
/// catalog's, so they can only reliably be dropped once the whole transaction has committed or rolled back.
class ScopedSecrets : public ClientContextState {
public:
	static ScopedSecrets &Get(ClientContext &context) {
		return *context.registered_state->GetOrCreate<ScopedSecrets>(kScopedSecretsStateKey);
	}

	/// Creates a secret for a table's vended credentials.
	void Create(ClientContext &context, const string &catalog_name, const iceberg::Table &table) {
		auto input = BuildScopedS3Secret(table);
		// The transaction ID and index make the name unique within the database instance.
		input.name = StringUtil::Format("__proto_ic_%s_%s_%s", catalog_name,
		                                std::to_string(MetaTransaction::Get(context).global_transaction_id),
		                                std::to_string(names_.size()));
		if (!SecretManager::Get(context).CreateSecret(context, input)) {
			throw IOException("Failed to create scoped S3 secret '%s' for table '%s'", input.name,
			                  table.name().ToString());
		}
		names_.push_back(std::move(input.name));
	}

	void TransactionCommit(MetaTransaction &, ClientContext &context) override {
		DropAll(context);
	}

	void TransactionRollback(MetaTransaction &, ClientContext &context) override {
		DropAll(context);
	}

private:
	/// Drops the secrets through a new connection, as the ended transaction can no longer be used.
	void DropAll(ClientContext &context) {
		auto names = std::exchange(names_, {});
		// Most transactions create no secrets; don't open a connection for them.
		if (names.empty()) {
			return;
		}

		try {
			Connection con(*context.db);
			con.BeginTransaction();
			auto &secret_manager = SecretManager::Get(*con.context);
			for (const auto &name : names) {
				secret_manager.DropSecretByName(*con.context, name, OnEntryNotFound::RETURN_NULL,
				                                SecretPersistType::TEMPORARY);
			}
			// Commit, not Rollback: the secret drop is a transactional catalog op, so rolling back would revert it.
			con.Commit();
		} catch (std::exception &ex) {
			// Leaked secrets are TEMPORARY and uniquely named, so they neither persist nor clash with later ones.
			DUCKDB_LOG_WARNING(context, "proto_iceberg: failed to drop scoped S3 secret(s): %s", ex.what());
		}
	}

	vector<string> names_;
};

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
	if (!scan_info_ || !scan_info_->table) {
		throw InternalException("Cannot scan Iceberg table '%s' before it is fully loaded", name);
	}

	// httpfs provides the S3 secret type; load it before creating the scoped S3 secret.
	ExtensionHelper::AutoLoadExtension(DatabaseInstance::GetDatabase(context), kHttpfsExtension);
	// The entry lives for one transaction and pins its table, so its scans share one secret.
	// TODO: Support credential refresh (in some manner) within a transaction, which would change this.
	if (!has_secret_) {
		ScopedSecrets::Get(context).Create(context, catalog.GetName(), *scan_info_->table);
		has_secret_ = true;
	}

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
