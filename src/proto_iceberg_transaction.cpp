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

void ProtoIcebergTransaction::TrackSecret(std::string_view secret_name) {
	created_secrets_.Lock()->insert(string(secret_name));
}

bool ProtoIcebergTransaction::HasTrackedSecret(std::string_view secret_name) const {
	auto secrets = created_secrets_.Lock();
	return secrets->contains(string(secret_name));
}

void ProtoIcebergTransaction::DropSecrets(ClientContext &context) {
	auto secrets = created_secrets_.Lock();
	if (secrets->empty()) {
		return;
	}
	auto &secret_manager = SecretManager::Get(context);
	for (auto &secret_name : *secrets) {
		secret_manager.DropSecretByName(context, secret_name, OnEntryNotFound::RETURN_NULL,
		                                SecretPersistType::TEMPORARY);
	}
	secrets->clear();
}

ProtoIcebergTransaction &ProtoIcebergTransaction::Get(ClientContext &context, AttachedDatabase &db) {
	return Transaction::Get(context, db).Cast<ProtoIcebergTransaction>();
}

ProtoIcebergTransactionManager::ProtoIcebergTransactionManager(AttachedDatabase &db_p, ProtoIcebergCatalog &catalog)
    : TransactionManager(db_p), catalog_(catalog) {
}

ProtoIcebergTransactionManager::~ProtoIcebergTransactionManager() = default;

void ProtoIcebergTransactionManager::DropSecretsInNestedTxn(ProtoIcebergTransaction &txn) const {
	// Secret drop is a catalog write that can't run inside the txn; use a temporary Connection with its own txn.
	Connection temp_con(db.GetDatabase());
	temp_con.BeginTransaction();
	txn.DropSecrets(*temp_con.context);
	// N.B. Commit (not Rollback); the secret drop is a transactional catalog op, so rolling back would revert it.
	temp_con.Commit();
}

Transaction &ProtoIcebergTransactionManager::StartTransaction(ClientContext &context) {
	auto transaction = make_uniq<ProtoIcebergTransaction>(*this, context);
	auto &result = *transaction;
	auto guard = transactions_.Lock();
	(*guard)[result] = std::move(transaction);
	return result;
}

ErrorData ProtoIcebergTransactionManager::CommitTransaction(ClientContext &context, Transaction &transaction) {
	auto &txn = transaction.Cast<ProtoIcebergTransaction>();

	try {
		DropSecretsInNestedTxn(txn);
	} catch (std::exception &ex) {
		// Bounded leak: secrets are TEMPORARY + per-txn-unique + REPLACE_ON_CONFLICT.
		DUCKDB_LOG_WARNING(context, "proto_iceberg: failed to drop scoped S3 secret(s) on commit: %s", ex.what());
	}

	auto guard = transactions_.Lock();
	if (auto it = guard->find(transaction); it != guard->end()) {
		guard->erase(it);
		return {};
	}
	return ErrorData {ExceptionType::TRANSACTION, "Cannot commit a transaction that is not active"};
}

void ProtoIcebergTransactionManager::RollbackTransaction(Transaction &transaction) {
	auto &txn = transaction.Cast<ProtoIcebergTransaction>();

	try {
		DropSecretsInNestedTxn(txn);
	} catch (std::exception &) {
		// Bounded leak: secrets are TEMPORARY + per-txn-unique + REPLACE_ON_CONFLICT.
	}

	auto guard = transactions_.Lock();
	if (auto it = guard->find(transaction); it != guard->end()) {
		guard->erase(it);
		return;
	}
	throw TransactionException("Cannot rollback a transaction that is not active");
}

void ProtoIcebergTransactionManager::Checkpoint(ClientContext &context, bool force) {
}

} // namespace duckdb
