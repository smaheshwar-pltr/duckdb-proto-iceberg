#include "catch.hpp"
#include "mutex.hpp"

#include <latch>
#include <string>
#include <thread>
#include <vector>

using namespace duckdb;

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

TEST_CASE("Moving a Guard transfers the lock", "[mutex]") {
	Mutex<int> m(std::in_place, 10);
	{
		auto g1 = m.Lock();
		auto g2 = std::move(g1);
		REQUIRE(*g2 == 10);
	}
	// The lock moved to g2 and was released with it, so this doesn't deadlock.
	auto g3 = m.Lock();
	REQUIRE(*g3 == 10);
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
