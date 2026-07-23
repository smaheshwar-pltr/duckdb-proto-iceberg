#define DUCKDB_EXTENSION_MAIN

#include "constants.hpp"
#include "proto_iceberg_extension.hpp"
#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_transaction.hpp"
#include "duckdb.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/main/extension_helper.hpp"

#include "iceberg/arrow/arrow_register.h"
#include "iceberg/avro/avro_register.h"

namespace duckdb {
namespace {

using constants::kEndpoint;
using constants::kIcebergSecretType;
using constants::kRedactedSecrets;
using constants::kToken;

const string kConfigProvider = "config";
const string kHttpfsExtension = "httpfs";
const string kIcebergExtensionName = "proto_iceberg";
const string kStorageExtensionName = "iceberg";

unique_ptr<TransactionManager> CreateIcebergTransactionManager(optional_ptr<StorageExtensionInfo>, AttachedDatabase &db,
                                                               Catalog &catalog) {
	auto &ic_catalog = catalog.Cast<ProtoIcebergCatalog>();
	return make_uniq<ProtoIcebergTransactionManager>(db, ic_catalog);
}

class ProtoIcebergStorageExtension : public StorageExtension {
public:
	ProtoIcebergStorageExtension() {
		attach = ProtoIcebergCatalog::Attach;
		create_transaction_manager = CreateIcebergTransactionManager;
	}
};

unique_ptr<BaseSecret> CreateIcebergSecret(ClientContext &, CreateSecretInput &input) {
	auto result = make_uniq<KeyValueSecret>(input.scope, input.type, input.provider, input.name);
	for (auto &[key, value] : input.options) {
		result->secret_map[key] = value;
	}
	result->redact_keys.insert_range(kRedactedSecrets);
	return result;
}

void RegisterIcebergFileIO() {
	iceberg::arrow::RegisterAll();
	iceberg::avro::RegisterAll();
}

void RegisterIcebergSecretType(ExtensionLoader &loader) {
	SecretType iceberg_secret_type;
	iceberg_secret_type.name = kIcebergSecretType;
	iceberg_secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	iceberg_secret_type.default_provider = kConfigProvider;
	loader.RegisterSecretType(iceberg_secret_type);
}

void RegisterIcebergSecretFunction(ExtensionLoader &loader) {
	CreateSecretFunction secret_function {.secret_type = kIcebergSecretType,
	                                      .provider = kConfigProvider,
	                                      .function = CreateIcebergSecret,
	                                      .named_parameters = {}};
	secret_function.named_parameters[kEndpoint] = LogicalType::VARCHAR;
	secret_function.named_parameters[kToken] = LogicalType::VARCHAR;
	loader.RegisterFunction(secret_function);
}

} // namespace

void LoadInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();

	ExtensionHelper::AutoLoadExtension(instance, kHttpfsExtension);

	RegisterIcebergFileIO();

	RegisterIcebergSecretType(loader);
	RegisterIcebergSecretFunction(loader);

	auto &config = DBConfig::GetConfig(instance);
	StorageExtension::Register(config, kStorageExtensionName, make_shared_ptr<ProtoIcebergStorageExtension>());
}

void ProtoIcebergExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

string ProtoIcebergExtension::Name() {
	return kIcebergExtensionName;
}

string ProtoIcebergExtension::Version() const {
#ifdef EXT_VERSION_PROTO_ICEBERG
	return EXT_VERSION_PROTO_ICEBERG;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(proto_iceberg, loader) {
	duckdb::LoadInternal(loader);
}
}
