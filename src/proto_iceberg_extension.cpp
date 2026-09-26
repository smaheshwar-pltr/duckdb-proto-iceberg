#define DUCKDB_EXTENSION_MAIN

#include "constants.hpp"
#include "proto_iceberg_extension.hpp"
#include "proto_iceberg_catalog.hpp"
#include "proto_iceberg_transaction.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/main/extension_helper.hpp"

#include "iceberg/arrow/arrow_register.h"
#include "iceberg/avro/avro_register.h"

#include <cstdlib>

namespace duckdb {
namespace {

using constants::kConfigProvider;
using constants::kEndpoint;
using constants::kIcebergSecretType;
using constants::kRedactedSecrets;
using constants::kToken;

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
	result->redact_keys.insert(kRedactedSecrets.begin(), kRedactedSecrets.end());
	return result;
}

void RegisterIcebergFileIO() {
	iceberg::arrow::RegisterAll();
	iceberg::avro::RegisterAll();
}

/// Turns off the AWS SDK's logging unless the user configured it, before iceberg-cpp initializes Arrow's S3 support.
///
/// When Arrow finalizes S3 at exit, the AWS CRT can still be shutting down an event loop on another thread, which
/// logs through the SDK's logger after the SDK has torn it down and crashes. With logging off the SDK installs no
/// logger. Arrow reads ARROW_S3_LOG_LEVEL when S3 is first initialized. The variable is process-wide, so it also
/// applies to other Arrow S3 users in the process and to child processes.
void DisableAwsSdkLogging() {
	static constexpr const char *kArrowS3LogLevel = "ARROW_S3_LOG_LEVEL";
#ifdef _WIN32
	if (!std::getenv(kArrowS3LogLevel)) {
		_putenv_s(kArrowS3LogLevel, "off");
	}
#else
	setenv(kArrowS3LogLevel, "off", /*overwrite=*/0);
#endif
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

void LoadInternal(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();

	DisableAwsSdkLogging();
	RegisterIcebergFileIO();

	RegisterIcebergSecretType(loader);
	RegisterIcebergSecretFunction(loader);

	auto &config = DBConfig::GetConfig(instance);
	StorageExtension::Register(config, kStorageExtensionName, make_shared_ptr<ProtoIcebergStorageExtension>());
}

} // namespace

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
