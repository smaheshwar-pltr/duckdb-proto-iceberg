#pragma once

#include "duckdb/common/assert.hpp"
#include "duckdb/common/mutex.hpp"
#include <utility>

namespace duckdb {

/// A value that can only be accessed while holding its mutex, similar to Rust's std::sync::Mutex.
///
/// Lock() acquires the mutex and returns a guard that dereferences to the value. The mutex is released when the guard
/// is destroyed. Locking a const Mutex returns a ConstGuard, which gives read-only access. A guard can be moved, for
/// example to return it from a function, and the lock moves with it.
///
///   Mutex<vector<int>> values;
///   values.Lock()->push_back(1); // locked for this statement only
///   auto guard = values.Lock();  // locked until guard is destroyed
///   guard->push_back(2);
///
/// A guard must not outlive its Mutex, and references to the value must not outlive the guard they came from.
template <typename T>
class Mutex {
private:
	template <typename U>
	class GuardImpl {
		friend class Mutex;

	public:
		U &operator*() {
			D_ASSERT(lock_.owns_lock());
			return data_;
		}
		U *operator->() {
			return &operator*();
		}
		const U &operator*() const {
			D_ASSERT(lock_.owns_lock());
			return data_;
		}
		const U *operator->() const {
			return &operator*();
		}

	private:
		GuardImpl(mutex &m, U &data) : lock_(m), data_(data) {
		}

		unique_lock<mutex> lock_;
		U &data_;
	};

public:
	using Guard = GuardImpl<T>;
	using ConstGuard = GuardImpl<const T>;

	Mutex() = default;

	template <typename... Args>
	explicit Mutex(std::in_place_t, Args &&...args) : data_(std::forward<Args>(args)...) {
	}

	[[nodiscard]] Guard Lock() {
		return Guard(mutex_, data_);
	}
	[[nodiscard]] ConstGuard Lock() const {
		return ConstGuard(mutex_, data_);
	}

private:
	mutable mutex mutex_;
	T data_ {};
};

} // namespace duckdb
