/// SignedStore<Key, Value> — a three-state store.
///
/// Every entry is in one of three states:
///   Unknown  — never looked up
///   Negative — looked up and confirmed absent
///   Positive — looked up and found; the store owns the entry
///
/// Not thread-safe.

#pragma once

#include <map>
#include <variant>

namespace duckdb {

template <typename Key, typename Value>
class SignedStore {
public:
	struct Unknown {};
	struct Negative {};
	struct Positive {
		// Points into the store; valid only until the next mutation. Don't mutate the store while visiting.
		Value *value;
	};
	using LookupResult = std::variant<Unknown, Negative, Positive>;

	/// Looks up an entry by key.
	[[nodiscard]] LookupResult Lookup(const Key &key) {
		auto it = entries_.find(key);
		if (it == entries_.end()) {
			return Unknown {};
		}
		if (auto *val = std::get_if<Value>(&it->second)) {
			return Positive {val};
		}
		return Negative {};
	}

	/// Stores a positive entry (unconditional).
	Value &Put(const Key &key, Value value) {
		auto [it, _] = entries_.insert_or_assign(key, StoredValue {std::move(value)});
		return std::get<Value>(it->second);
	}

	/// Stores a negative entry (unconditional).
	void PutMiss(const Key &key) {
		entries_.insert_or_assign(key, std::monostate {});
	}

	/// Iterates over all positive entries.
	template <typename Fn>
	void ForEachPositive(Fn &&fn) {
		for (auto &[key, stored] : entries_) {
			if (auto *val = std::get_if<Value>(&stored)) {
				fn(key, *val);
			}
		}
	}

private:
	using StoredValue = std::variant<std::monostate, Value>;
	std::map<Key, StoredValue> entries_;
};

} // namespace duckdb
