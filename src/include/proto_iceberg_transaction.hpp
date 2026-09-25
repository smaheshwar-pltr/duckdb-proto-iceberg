#pragma once

#include "proto_iceberg_schema_entry.hpp"
#include "mutex.hpp"

#include "duckdb/transaction/transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/common/reference_map.hpp"
#include "duckdb/main/client_context_state.hpp"

#include "iceberg/table_identifier.h"

#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

	/// Tracks that a temporary DuckDB secret was created for a table's vended credentials.
	void TrackSecret(const iceberg::TableIdentifier &table);

	/// Returns whether a secret was already created for this table.
	bool HasTrackedSecret(const iceberg::TableIdentifier &table) const;

	/// Returns the number of secrets created in this transaction.
	idx_t TrackedSecretCount() const;

	/// Gets the ProtoIcebergTransaction from a ClientContext.
	static ProtoIcebergTransaction &Get(ClientContext &context, AttachedDatabase &db);

private:
	int64_t start_timestamp_ms_;
	Mutex<LockedSchemas::SchemaState> schema_state_;
	/// Tables, by namespace levels and name, with a scoped secret.
	Mutex<std::set<std::pair<std::vector<string>, string>>> secret_tables_;
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
	ProtoIcebergCatalog &catalog_;
	Mutex<reference_map_t<Transaction, unique_ptr<ProtoIcebergTransaction>>> transactions_;
};

/// Drops the temporary secrets a connection creates for vended credentials once its transaction ends.
/// N.B. Secrets are written in the connection's system catalog transaction, which DuckDB may commit after the Iceberg
/// catalog's, so they can only reliably be dropped once the whole transaction has committed or rolled back.
class ProtoIcebergSecretCleanup : public ClientContextState {
public:
	/// Gets the connection's cleanup state, registering it on first use.
	static ProtoIcebergSecretCleanup &Get(ClientContext &context);

	/// Schedules a secret to be dropped when the current transaction ends.
	void Track(std::string_view secret_name);

	void TransactionCommit(MetaTransaction &transaction, ClientContext &context) override;
	void TransactionRollback(MetaTransaction &transaction, ClientContext &context) override;

private:
	/// Drops the tracked secrets via a nested Connection, as the ended transaction can no longer be used.
	void DropSecrets(ClientContext &context);

	Mutex<std::vector<string>> secrets_;
};

} // namespace duckdb
