#pragma once

#include "duckdb/common/assert.hpp"
#include "duckdb/common/mutex.hpp"
#include <utility>

namespace duckdb {

/// A value only accessible through Lock(), whose guard holds the mutex. A guard must not outlive its Mutex.
template <typename T>
class Mutex {
public:
	Mutex() : data_() {
	}

	template <typename... Args>
	explicit Mutex(std::in_place_t, Args &&...args) : data_(std::forward<Args>(args)...) {
	}

	Mutex(const Mutex &) = delete;
	Mutex &operator=(const Mutex &) = delete;
	Mutex(Mutex &&) = delete;
	Mutex &operator=(Mutex &&) = delete;

	template <typename U>
	class GuardImpl {
		friend class Mutex;

	public:
		GuardImpl(GuardImpl &&other) noexcept : lock_(std::move(other.lock_)), data_(other.data_) {
			other.data_ = nullptr;
		}
		GuardImpl &operator=(GuardImpl &&) = delete;
		GuardImpl(const GuardImpl &) = delete;
		GuardImpl &operator=(const GuardImpl &) = delete;

		U &operator*() {
			D_ASSERT(data_);
			return *data_;
		}
		U *operator->() {
			D_ASSERT(data_);
			return data_;
		}
		const U &operator*() const {
			D_ASSERT(data_);
			return *data_;
		}
		const U *operator->() const {
			D_ASSERT(data_);
			return data_;
		}

	private:
		GuardImpl(mutex &m, U &data) : lock_(m), data_(&data) {
		}

		unique_lock<mutex> lock_;
		U *data_;
	};

	using Guard = GuardImpl<T>;
	using ConstGuard = GuardImpl<const T>;

	[[nodiscard]] Guard Lock() {
		return Guard(mutex_, data_);
	}
	[[nodiscard]] ConstGuard Lock() const {
		return ConstGuard(mutex_, data_);
	}

private:
	mutable mutex mutex_;
	T data_;
};

} // namespace duckdb
