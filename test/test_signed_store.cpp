#include "catch.hpp"
#include "signed_store.hpp"

#include <utility>

using namespace duckdb;

TEST_CASE("unknown key returns Unknown", "[signed_store]") {
	SignedStore<std::string, int> store;
	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, int>::Unknown>(result));
}

TEST_CASE("inserted key returns Positive", "[signed_store]") {
	SignedStore<std::string, int> store;
	auto &val = store.Put("foo", 42);
	REQUIRE(val == 42);

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, int>::Positive>(result));
	auto &positive = std::get<SignedStore<std::string, int>::Positive>(result);
	REQUIRE(*positive.value == 42);
}

TEST_CASE("missed key returns Negative", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.PutMiss("foo");

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, int>::Negative>(result));
}

TEST_CASE("PutMiss overwrites positive entry", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.Put("foo", 42);
	store.PutMiss("foo");

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, int>::Negative>(result));
}

TEST_CASE("Put overwrites existing value", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.Put("foo", 1);
	auto &val = store.Put("foo", 2);
	REQUIRE(val == 2);

	auto result = store.Lookup("foo");
	auto &positive = std::get<SignedStore<std::string, int>::Positive>(result);
	REQUIRE(*positive.value == 2);
}

TEST_CASE("Put promotes Negative to Positive", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.PutMiss("foo");

	auto &val = store.Put("foo", 7);
	REQUIRE(val == 7);

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, int>::Positive>(result));
}

TEST_CASE("ForEachPositive skips negative entries and yields keys", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.Put("a", 1);
	store.PutMiss("b");
	store.Put("c", 3);

	std::vector<std::pair<std::string, int>> entries;
	store.ForEachPositive([&](const std::string &k, int &v) { entries.emplace_back(k, v); });

	// std::map iterates in key order, so no sort needed.
	std::vector<std::pair<std::string, int>> expected = {{"a", 1}, {"c", 3}};
	REQUIRE(entries == expected);
}
