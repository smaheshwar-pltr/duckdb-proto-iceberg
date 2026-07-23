#pragma once

#include "proto_iceberg_schema_entry.hpp"
#include "mutex.hpp"

#include "duckdb/transaction/transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/common/reference_map.hpp"

#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>

namespace duckdb {

class ProtoIcebergCatalog;

class ProtoIcebergTransaction : public Transaction {
public:
	ProtoIcebergTransaction(TransactionManager &manager, ClientContext &context);
	~ProtoIcebergTransaction() override;

	/// Gets the transaction start timestamp in milliseconds since Unix epoch.
	int64_t GetStartTimestampMs() const {
		return start_timestamp_ms_;
	}

	class LockedSchemas {
		friend class ProtoIcebergTransaction;

	public:
		~LockedSchemas() = default;
		LockedSchemas(LockedSchemas &&) = default;
		LockedSchemas(const LockedSchemas &) = delete;
		LockedSchemas &operator=(const LockedSchemas &) = delete;

		optional_ptr<ProtoIcebergSchemaEntry> Lookup(std::string_view name) const;
		ProtoIcebergSchemaEntry &Store(std::string_view name, unique_ptr<ProtoIcebergSchemaEntry> entry);

		/// Returns whether schemas have been listed.
		bool Listed() const;
		/// Signals that schemas have been listed.
		void MarkListed();

		/// Iterates present schema entries.
		template <typename Fn>
		void ForEach(Fn &&fn) {
			for (auto &entry : guard_->schemas | std::views::values) {
				if (!entry->NamespaceNotFound()) {
					fn(*entry);
				}
			}
		}

	private:
		struct SchemaState {
			map<string, unique_ptr<ProtoIcebergSchemaEntry>> schemas;
			bool listed = false;
		};

		explicit LockedSchemas(Mutex<SchemaState>::Guard guard);
		Mutex<SchemaState>::Guard guard_;
	};

	/// Acquires the schema store lock. Operations are performed through the returned handle.
	LockedSchemas LockSchemas();

	/// Tracks a temporary DuckDB secret created for vended credentials.
	void TrackSecret(std::string_view secret_name);

	/// Returns whether a secret with this name is already tracked.
	bool HasTrackedSecret(std::string_view secret_name) const;

	/// Drops all tracked temporary secrets.
	void DropSecrets(ClientContext &context);

	/// Gets the ProtoIcebergTransaction from a ClientContext.
	static ProtoIcebergTransaction &Get(ClientContext &context, AttachedDatabase &db);

private:
	int64_t start_timestamp_ms_;
	Mutex<LockedSchemas::SchemaState> schema_state_;
	Mutex<std::set<string>> created_secrets_;
};

class ProtoIcebergTransactionManager : public TransactionManager {
public:
	ProtoIcebergTransactionManager(AttachedDatabase &db_p, ProtoIcebergCatalog &catalog);
	~ProtoIcebergTransactionManager() override;

	Transaction &StartTransaction(ClientContext &context) override;
	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override;
	void RollbackTransaction(Transaction &transaction) override;
	void Checkpoint(ClientContext &context, bool force) override;

private:
	/// Drops a transaction's scoped secrets via a nested Connection.
	void DropSecretsInNestedTxn(ProtoIcebergTransaction &txn) const;

	ProtoIcebergCatalog &catalog_;
	Mutex<reference_map_t<Transaction, unique_ptr<ProtoIcebergTransaction>>> transactions_;
};

} // namespace duckdb
