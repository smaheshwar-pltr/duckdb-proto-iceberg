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

TEST_CASE("PutMiss overwrites unique_ptr positive entry", "[signed_store]") {
	SignedStore<std::string, std::unique_ptr<int>> store;
	store.Put("foo", std::make_unique<int>(42));
	store.PutMiss("foo");

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, std::unique_ptr<int>>::Negative>(result));
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

TEST_CASE("Put on missing key creates new entry", "[signed_store]") {
	SignedStore<std::string, int> store;
	auto &val = store.Put("foo", 99);
	REQUIRE(val == 99);
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

TEST_CASE("ForEachPositive on empty store does nothing", "[signed_store]") {
	SignedStore<std::string, int> store;
	int count = 0;
	store.ForEachPositive([&](const std::string &, int &) { count++; });
	REQUIRE(count == 0);
}

TEST_CASE("ForEachPositive yields correct key for single entry", "[signed_store]") {
	SignedStore<std::string, int> store;
	store.Put("only_key", 77);

	std::string seen_key;
	store.ForEachPositive([&](const std::string &k, int &) { seen_key = k; });
	REQUIRE(seen_key == "only_key");
}

TEST_CASE("unique_ptr values", "[signed_store]") {
	SignedStore<std::string, std::unique_ptr<int>> store;
	store.Put("foo", std::make_unique<int>(42));

	auto result = store.Lookup("foo");
	REQUIRE(std::holds_alternative<SignedStore<std::string, std::unique_ptr<int>>::Positive>(result));
	auto &positive = std::get<SignedStore<std::string, std::unique_ptr<int>>::Positive>(result);
	REQUIRE(**positive.value == 42);
}

TEST_CASE("Put overwrites unique_ptr values", "[signed_store]") {
	SignedStore<std::string, std::unique_ptr<int>> store;
	store.Put("foo", std::make_unique<int>(1));
	store.Put("foo", std::make_unique<int>(2));

	auto result = store.Lookup("foo");
	auto &positive = std::get<SignedStore<std::string, std::unique_ptr<int>>::Positive>(result);
	REQUIRE(**positive.value == 2);
}

TEST_CASE("SignedStore with pair key", "[signed_store]") {
	using Key = std::pair<std::string, std::string>;
	SignedStore<Key, int> store;
	auto key = Key {"ns", "table"};
	auto &val = store.Put(key, 42);
	REQUIRE(val == 42);

	auto result = store.Lookup(key);
	REQUIRE(std::holds_alternative<SignedStore<Key, int>::Positive>(result));
	REQUIRE(*std::get<SignedStore<Key, int>::Positive>(result).value == 42);

	auto missing_key = Key {"ns", "other"};
	auto missing = store.Lookup(missing_key);
	REQUIRE(std::holds_alternative<SignedStore<Key, int>::Unknown>(missing));

	store.PutMiss(missing_key);
	auto neg = store.Lookup(missing_key);
	REQUIRE(std::holds_alternative<SignedStore<Key, int>::Negative>(neg));
}
