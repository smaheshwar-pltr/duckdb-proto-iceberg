#pragma once

#include <map>
#include <variant>

namespace duckdb {

/// A map whose keys are each unknown, negative (known absent) or positive (found). Not thread-safe.
template <typename Key, typename Value>
class SignedStore {
public:
	/// An entry's state, as returned by Lookup.
	struct LookupResult {
		struct Unknown {};
		struct Negative {};
		struct Positive {
			// Points into the store, so it's only valid until the next mutation.
			Value *value;
		};

		using Variant = std::variant<Unknown, Negative, Positive>;
	};

	[[nodiscard]] typename LookupResult::Variant Lookup(const Key &key) {
		auto it = entries_.find(key);
		if (it == entries_.end()) {
			return typename LookupResult::Unknown {};
		}
		if (auto *val = std::get_if<Value>(&it->second)) {
			return typename LookupResult::Positive {val};
		}
		return typename LookupResult::Negative {};
	}

	/// Stores a positive entry, replacing any previous one.
	Value &Put(const Key &key, Value value) {
		auto [it, _] = entries_.insert_or_assign(key, StoredValue {std::move(value)});
		return std::get<Value>(it->second);
	}

	/// Stores a negative entry, replacing any previous one.
	void PutMiss(const Key &key) {
		entries_.insert_or_assign(key, std::monostate {});
	}

	/// Visits positive entries in key order; fn must not mutate the store.
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
