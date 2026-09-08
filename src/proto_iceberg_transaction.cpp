#include "proto_iceberg_transaction.hpp"
#include "proto_iceberg_catalog.hpp"

#include "duckdb/transaction/transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/common/exception/transaction_exception.hpp"
#include "duckdb/common/types/timestamp.hpp"

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

} // namespace duckdb
