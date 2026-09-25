#include "catch.hpp"
#include "unwrap.hpp"

#include "iceberg/result.h"

#include <memory>
#include <string>
#include <type_traits>

using namespace duckdb;

namespace {

iceberg::Result<int> Ok(int v) {
	return v;
}

iceberg::Result<int> Err(std::string message) {
	return std::unexpected(iceberg::Error {.kind = iceberg::ErrorKind::kIOError, .message = std::move(message)});
}

} // namespace

TEST_CASE("UnwrapOrThrow returns the value on success", "[unwrap]") {
	auto result = Ok(42);
	REQUIRE(UnwrapOrThrow(result, "should not throw") == 42);
}

TEST_CASE("UnwrapOrThrow moves out of an rvalue result", "[unwrap]") {
	iceberg::Result<std::unique_ptr<int>> result = std::make_unique<int>(7);
	auto ptr = UnwrapOrThrow(std::move(result), "should not throw");
	REQUIRE(ptr != nullptr);
	REQUIRE(*ptr == 7);
}

TEST_CASE("UnwrapOrThrow returns an lvalue result's value by reference and an rvalue's by value", "[unwrap]") {
	iceberg::Result<std::string> result = std::string("value");
	const auto &const_result = result;
	static_assert(std::is_same_v<decltype(UnwrapOrThrow(result, "")), std::string &>);
	static_assert(std::is_same_v<decltype(UnwrapOrThrow(const_result, "")), const std::string &>);
	static_assert(std::is_same_v<decltype(UnwrapOrThrow(std::move(result), "")), std::string>);
	static_assert(std::is_void_v<decltype(UnwrapOrThrow(iceberg::Status {}, ""))>);

	UnwrapOrThrow(result, "should not throw") += "!";
	REQUIRE(result.value() == "value!");

	// Binding a temporary result's value to a const reference extends the value's lifetime instead of dangling.
	const auto &value = UnwrapOrThrow(iceberg::Result<std::string>(std::string(64, 'x')), "should not throw");
	REQUIRE(value == std::string(64, 'x'));
}

TEST_CASE("UnwrapOrThrow throws with the formatted message and error text appended", "[unwrap]") {
	auto result = Err("disk gone");
	std::string ns = "ns";
	std::string tbl = "tbl";
	try {
		UnwrapOrThrow(result, "Failed to read table '%s.%s'", ns, tbl);
		FAIL("expected UnwrapOrThrow to throw");
	} catch (const IOException &e) {
		std::string message = e.what();
		REQUIRE(message.find("Failed to read table 'ns.tbl'") != std::string::npos);
		REQUIRE(message.find("disk gone") != std::string::npos);
	}
}

TEST_CASE("UnwrapOrThrow honors the templated exception type", "[unwrap]") {
	auto result = Err("bad config");
	REQUIRE_THROWS_AS(UnwrapOrThrow<InvalidConfigurationException>(result, "nope"), InvalidConfigurationException);
}
