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

class ProtoIcebergTransaction : public Transaction {
public:
	ProtoIcebergTransaction(TransactionManager &manager, ClientContext &context);
	~ProtoIcebergTransaction() override;

	/// Returns the transaction's start time in Unix milliseconds.
	int64_t GetStartTimestampMs() const {
		return start_timestamp_ms_;
	}

	class LockedSchemas {
		friend class ProtoIcebergTransaction;

	public:
		LockedSchemas(LockedSchemas &&) = default;

		optional_ptr<ProtoIcebergSchemaEntry> Lookup(std::string_view name) const;
		ProtoIcebergSchemaEntry &Store(std::string_view name, unique_ptr<ProtoIcebergSchemaEntry> entry);

		/// Returns whether schemas have been listed.
		bool Listed() const;
		/// Records that schemas have been listed, marking those absent from the listing as missing.
		void CompleteListing(const std::set<std::string_view> &listed_names);

		/// Visits schemas not known to be missing.
		template <typename Fn>
		void ForEach(Fn &&fn) {
			for (auto &entry : guard_->schemas | std::views::values) {
				if (!entry->IsNamespaceMissing()) {
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

	/// Locks the transaction's schemas, which are accessed through the returned handle.
	LockedSchemas LockSchemas();

	static ProtoIcebergTransaction &Get(ClientContext &context, AttachedDatabase &db);

private:
	int64_t start_timestamp_ms_;
	Mutex<LockedSchemas::SchemaState> schema_state_;
};

class ProtoIcebergTransactionManager : public TransactionManager {
public:
	explicit ProtoIcebergTransactionManager(AttachedDatabase &db_p);
	~ProtoIcebergTransactionManager() override;

	Transaction &StartTransaction(ClientContext &context) override;
	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override;
	void RollbackTransaction(Transaction &transaction) override;
	void Checkpoint(ClientContext &context, bool force) override;

private:
	Mutex<reference_map_t<Transaction, unique_ptr<ProtoIcebergTransaction>>> transactions_;
};

} // namespace duckdb
