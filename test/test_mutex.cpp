#include "catch.hpp"
#include "mutex.hpp"

#include <atomic>
#include <latch>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace duckdb;

TEST_CASE("Mutex default-constructs the inner value", "[mutex]") {
	Mutex<int> m;
	REQUIRE(*m.Lock() == 0);
}

TEST_CASE("Mutex forwards in-place constructor arguments", "[mutex]") {
	Mutex<std::string> m(std::in_place, 5, 'x');
	REQUIRE(*m.Lock() == "xxxxx");
}

TEST_CASE("Guard operator* and operator-> access the inner value", "[mutex]") {
	Mutex<std::string> m(std::in_place, "hello");
	auto guard = m.Lock();
	REQUIRE(*guard == "hello");
	REQUIRE(guard->size() == 5);
}

TEST_CASE("Guard allows mutating the inner value", "[mutex]") {
	Mutex<int> m;
	{ *m.Lock() = 42; }
	REQUIRE(*m.Lock() == 42);
}

TEST_CASE("Guard is move-constructible and the moved-to guard still works", "[mutex]") {
	Mutex<int> m(std::in_place, 99);
	auto g1 = m.Lock();
	auto g2 = std::move(g1);
	REQUIRE(*g2 == 99);
}

TEST_CASE("Lock on a const Mutex returns a ConstGuard that provides read-only access", "[mutex]") {
	struct Point {
		int x = 0;
		int y = 0;
	};
	Mutex<Point> m(std::in_place, Point {7, 42});
	const auto &cm = m;
	auto guard = cm.Lock();
	REQUIRE((*guard).x == 7);
	REQUIRE(guard->x == 7);
	REQUIRE(guard->y == 42);
	static_assert(std::is_const_v<std::remove_reference_t<decltype(*guard)>>);
}

TEST_CASE("Mutex with struct: operator-> accesses fields", "[mutex]") {
	struct State {
		int count = 0;
		bool ready = false;
	};

	Mutex<State> m;
	{
		auto guard = m.Lock();
		guard->count = 5;
		guard->ready = true;
	}
	auto guard = m.Lock();
	REQUIRE(guard->count == 5);
	REQUIRE(guard->ready);
}

TEST_CASE("Mutex with unique_ptr inner value", "[mutex]") {
	Mutex<std::unique_ptr<int>> m;
	{ *m.Lock() = std::make_unique<int>(42); }
	REQUIRE(**m.Lock() == 42);
}

TEST_CASE("Mutex with vector inner value", "[mutex]") {
	Mutex<std::vector<int>> m;
	{
		auto guard = m.Lock();
		guard->push_back(1);
		guard->push_back(2);
		guard->push_back(3);
	}
	auto guard = m.Lock();
	REQUIRE(*guard == std::vector<int> {1, 2, 3});
}

TEST_CASE("Moved-from Guard does not deadlock on destruction", "[mutex]") {
	Mutex<int> m(std::in_place, 10);
	{
		auto g1 = m.Lock();
		auto g2 = std::move(g1);
		// g1 is moved-from; both g1 and g2 are destroyed here.
	}
	// Must not deadlock — the lock was transferred to g2 and released on its destruction.
	auto g3 = m.Lock();
	REQUIRE(*g3 == 10);
}

TEST_CASE("Guard releases lock on exception unwinding", "[mutex]") {
	Mutex<int> m(std::in_place, 5);
	try {
		auto guard = m.Lock();
		*guard = 99;
		throw std::runtime_error("test");
	} catch (...) {
	}
	// Must not deadlock — the guard released the lock during stack unwinding.
	auto guard = m.Lock();
	REQUIRE(*guard == 99);
}

TEST_CASE("Concurrent increments are serialised", "[mutex]") {
	Mutex<int> m;
	constexpr int kThreads = 4;
	constexpr int kIncrementsPerThread = 10000;
	std::latch start_latch(kThreads);

	std::vector<std::thread> threads;
	threads.reserve(kThreads);
	for (int t = 0; t < kThreads; ++t) {
		threads.emplace_back([&m, &start_latch]() {
			start_latch.arrive_and_wait();
			for (int i = 0; i < kIncrementsPerThread; ++i) {
				++(*m.Lock());
			}
		});
	}
	for (auto &t : threads) {
		t.join();
	}
	REQUIRE(*m.Lock() == kThreads * kIncrementsPerThread);
}

TEST_CASE("Concurrent reader and writer see consistent struct state", "[mutex]") {
	struct State {
		int a = 0;
		int b = 0;
	};

	Mutex<State> m;
	constexpr int kIterations = 5000;
	std::latch start_latch(2);

	std::thread writer([&m, &start_latch]() {
		start_latch.arrive_and_wait();
		for (int i = 0; i < kIterations; ++i) {
			auto guard = m.Lock();
			guard->a = i;
			guard->b = i;
		}
	});

	// Catch2 REQUIRE is not thread-safe, so record consistency off-thread in an atomic and assert on the main thread.
	std::atomic consistent {true};
	std::thread reader([&m, &start_latch, &consistent]() {
		start_latch.arrive_and_wait();
		for (int i = 0; i < kIterations; ++i) {
			if (auto guard = m.Lock(); guard->a != guard->b) {
				consistent = false;
			}
		}
	});

	writer.join();
	reader.join();
	REQUIRE(consistent.load());
}
