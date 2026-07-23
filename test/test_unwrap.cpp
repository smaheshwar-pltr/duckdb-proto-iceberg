#include "catch.hpp"
#include "unwrap.hpp"

#include "iceberg/result.h"

#include <memory>
#include <string>

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
