#pragma once

#include "duckdb/common/assert.hpp"
#include "duckdb/common/mutex.hpp"
#include <memory>
#include <utility>

namespace duckdb {

/// A value only accessible through Lock(), whose guard holds the mutex. A guard must not outlive its Mutex.
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
			return std::addressof(operator*());
		}
		const U &operator*() const {
			D_ASSERT(lock_.owns_lock());
			return data_;
		}
		const U *operator->() const {
			return std::addressof(operator*());
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
