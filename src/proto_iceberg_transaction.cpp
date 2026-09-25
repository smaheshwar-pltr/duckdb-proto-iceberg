#include "proto_iceberg_transaction.hpp"
#include "proto_iceberg_catalog.hpp"

#include "duckdb/transaction/transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/logging/logger.hpp"

namespace duckdb {

ProtoIcebergTransaction::ProtoIcebergTransaction(TransactionManager &manager, ClientContext &context)
    : Transaction(manager, context) {
	// DuckDB's timestamp_t holds microseconds since epoch; iceberg-cpp expects milliseconds.
	auto duckdb_timestamp = MetaTransaction::Get(context).GetCurrentTransactionStartTimestamp();
	start_timestamp_ms_ = duckdb_timestamp.value / 1000;
}

ProtoIcebergTransaction::~ProtoIcebergTransaction() = default;

ProtoIcebergTransaction::LockedSchemas::LockedSchemas(Mutex<SchemaState>::Guard guard) : guard_(std::move(guard)) {
}

ProtoIcebergTransaction::LockedSchemas ProtoIcebergTransaction::LockSchemas() {
	return LockedSchemas(schema_state_.Lock());
}

optional_ptr<ProtoIcebergSchemaEntry> ProtoIcebergTransaction::LockedSchemas::Lookup(std::string_view name) const {
	if (auto it = guard_->schemas.find(string(name)); it != guard_->schemas.end()) {
		return it->second.get();
	}
	return nullptr;
}

ProtoIcebergSchemaEntry &ProtoIcebergTransaction::LockedSchemas::Store(std::string_view name,
                                                                       unique_ptr<ProtoIcebergSchemaEntry> entry) {
	auto &ref = *entry;
	guard_->schemas[string(name)] = std::move(entry);
	return ref;
}

bool ProtoIcebergTransaction::LockedSchemas::Listed() const {
	return guard_->listed;
}

void ProtoIcebergTransaction::LockedSchemas::MarkListed() {
	guard_->listed = true;
}

namespace {

std::pair<std::vector<string>, string> TableKey(const iceberg::TableIdentifier &table) {
	return {table.ns.levels, table.name};
}

} // namespace

void ProtoIcebergTransaction::TrackSecret(const iceberg::TableIdentifier &table) {
	secret_tables_.Lock()->insert(TableKey(table));
}

bool ProtoIcebergTransaction::HasTrackedSecret(const iceberg::TableIdentifier &table) const {
	return secret_tables_.Lock()->contains(TableKey(table));
}

idx_t ProtoIcebergTransaction::TrackedSecretCount() const {
	return secret_tables_.Lock()->size();
}

ProtoIcebergTransaction &ProtoIcebergTransaction::Get(ClientContext &context, AttachedDatabase &db) {
	return Transaction::Get(context, db).Cast<ProtoIcebergTransaction>();
}

ProtoIcebergTransactionManager::ProtoIcebergTransactionManager(AttachedDatabase &db_p, ProtoIcebergCatalog &catalog)
    : TransactionManager(db_p), catalog_(catalog) {
}

ProtoIcebergTransactionManager::~ProtoIcebergTransactionManager() = default;

Transaction &ProtoIcebergTransactionManager::StartTransaction(ClientContext &context) {
	auto transaction = make_uniq<ProtoIcebergTransaction>(*this, context);
	auto &result = *transaction;
	auto guard = transactions_.Lock();
	(*guard)[result] = std::move(transaction);
	return result;
}

ErrorData ProtoIcebergTransactionManager::CommitTransaction(ClientContext &context, Transaction &transaction) {
	auto guard = transactions_.Lock();
	if (auto it = guard->find(transaction); it != guard->end()) {
		guard->erase(it);
		return {};
	}
	return ErrorData {ExceptionType::TRANSACTION, "Cannot commit a transaction that is not active"};
}

void ProtoIcebergTransactionManager::RollbackTransaction(Transaction &transaction) {
	auto guard = transactions_.Lock();
	if (auto it = guard->find(transaction); it != guard->end()) {
		guard->erase(it);
		return;
	}
	throw TransactionException("Cannot rollback a transaction that is not active");
}

void ProtoIcebergTransactionManager::Checkpoint(ClientContext &context, bool force) {
}

namespace {

const string kSecretCleanupStateKey = "proto_iceberg_secret_cleanup";

} // namespace

ProtoIcebergSecretCleanup &ProtoIcebergSecretCleanup::Get(ClientContext &context) {
	return *context.registered_state->GetOrCreate<ProtoIcebergSecretCleanup>(kSecretCleanupStateKey);
}

void ProtoIcebergSecretCleanup::Track(std::string_view secret_name) {
	secrets_.Lock()->emplace_back(secret_name);
}

void ProtoIcebergSecretCleanup::TransactionCommit(MetaTransaction &, ClientContext &context) {
	DropSecrets(context);
}

void ProtoIcebergSecretCleanup::TransactionRollback(MetaTransaction &, ClientContext &context) {
	DropSecrets(context);
}

void ProtoIcebergSecretCleanup::DropSecrets(ClientContext &context) {
	auto secrets = std::exchange(*secrets_.Lock(), {});
	// Most transactions create no secrets; don't open a Connection for them.
	if (secrets.empty()) {
		return;
	}

	try {
		Connection temp_con(*context.db);
		temp_con.BeginTransaction();
		auto &secret_manager = SecretManager::Get(*temp_con.context);
		for (const auto &secret_name : secrets) {
			secret_manager.DropSecretByName(*temp_con.context, secret_name, OnEntryNotFound::RETURN_NULL,
			                                SecretPersistType::TEMPORARY);
		}
		// N.B. Commit (not Rollback); the secret drop is a transactional catalog op, so rolling back would revert it.
		temp_con.Commit();
	} catch (std::exception &ex) {
		// Leaked secrets are TEMPORARY and uniquely named, so they neither persist nor clash with later ones.
		DUCKDB_LOG_WARNING(context, "proto_iceberg: failed to drop scoped S3 secret(s): %s", ex.what());
	}
}

} // namespace duckdb
