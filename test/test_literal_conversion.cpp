#include "catch.hpp"
#include "adapters.hpp"

#include "duckdb/common/types/value.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include <cmath>
#include <limits>
#include <variant>

using namespace duckdb;
using namespace duckdb::adapters;

TEST_CASE("boolean value", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::BOOLEAN(true));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "true");
}

TEST_CASE("tinyint widens to int32", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::TINYINT(42));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "42");
}

TEST_CASE("smallint widens to int32", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::SMALLINT(1000));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "1000");
}

TEST_CASE("integer", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::INTEGER(123456));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "123456");
}

TEST_CASE("bigint", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::BIGINT(999999999999LL));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "999999999999");
}

TEST_CASE("float approximation", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::FLOAT(3.14f));
	REQUIRE(lit.has_value());
	// Typed value compare rather than ToString (which depends on std::to_string formatting).
	REQUIRE(std::get<float>(lit->value()) == 3.14f);
}

TEST_CASE("double", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DOUBLE(2.5));
	REQUIRE(lit.has_value());
	REQUIRE(std::get<double>(lit->value()) == 2.5);
}

TEST_CASE("varchar", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value("hello world"));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "\"hello world\"");
}

TEST_CASE("date as days since epoch", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DATE(2024, 1, 15));
	REQUIRE(lit.has_value());
	// 2024-01-15 = day 19737 since epoch
	REQUIRE(lit->ToString() == "19737");
}

TEST_CASE("time as microseconds from midnight", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::TIME(12, 30, 0, 0));
	REQUIRE(lit.has_value());
	// 12:30:00 = 12*3600*1e6 + 30*60*1e6 = 45000000000
	REQUIRE(lit->ToString() == "45000000000");
}

TEST_CASE("timestamp as microseconds", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::TIMESTAMP(2024, 1, 1, 0, 0, 0, 0));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "1704067200000000");
}

TEST_CASE("timestamp_tz as microseconds", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::TIMESTAMPTZ(timestamp_tz_t(1704067200000000LL)));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "1704067200000000");
}

TEST_CASE("decimal(10,2) value", "[literal_conversion]") {
	// 12345 unscaled with scale 2 = 123.45
	auto lit = ConvertValueToLiteral(Value::DECIMAL(12345, 10, 2));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "123.45");
}

TEST_CASE("decimal(38,0) value", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DECIMAL(999999999, 38, 0));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "999999999");
}

TEST_CASE("decimal(5,5) value", "[literal_conversion]") {
	// 12345 unscaled with scale 5 = 0.12345
	auto lit = ConvertValueToLiteral(Value::DECIMAL(12345, 5, 5));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0.12345");
}

TEST_CASE("negative decimal", "[literal_conversion]") {
	// -500 unscaled with scale 2 = -5.00
	auto lit = ConvertValueToLiteral(Value::DECIMAL(-500, 10, 2));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "-5.00");
}

TEST_CASE("zero decimal", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DECIMAL(0, 10, 2));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0.00");
}

TEST_CASE("decimal(4,2) stored as int16", "[literal_conversion]") {
	// 99 unscaled with scale 2 = 0.99
	auto lit = ConvertValueToLiteral(Value::DECIMAL(static_cast<int16_t>(99), 4, 2));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0.99");
}

TEST_CASE("null returns nullopt", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value(LogicalType::INTEGER));
	REQUIRE_FALSE(lit.has_value());
}

TEST_CASE("unsupported type returns nullopt", "[literal_conversion]") {
	auto blob_lit = ConvertValueToLiteral(Value::BLOB("data"));
	REQUIRE_FALSE(blob_lit.has_value());
}

TEST_CASE("uuid returns nullopt", "[literal_conversion]") {
	auto uuid_lit = ConvertValueToLiteral(Value::UUID("550e8400-e29b-41d4-a716-446655440000"));
	REQUIRE_FALSE(uuid_lit.has_value());
}

TEST_CASE("negative integer", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::INTEGER(-1));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "-1");
}

TEST_CASE("negative bigint", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::BIGINT(-9999999999LL));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "-9999999999");
}

TEST_CASE("zero integer", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::INTEGER(0));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0");
}

TEST_CASE("zero bigint", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::BIGINT(0LL));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0");
}

TEST_CASE("zero float", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::FLOAT(0.0f));
	REQUIRE(lit.has_value());
	REQUIRE(std::get<float>(lit->value()) == 0.0f);
}

TEST_CASE("zero double", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DOUBLE(0.0));
	REQUIRE(lit.has_value());
	REQUIRE(std::get<double>(lit->value()) == 0.0);
}

TEST_CASE("boolean false", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::BOOLEAN(false));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "false");
}

TEST_CASE("empty string", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value(""));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "\"\"");
}

TEST_CASE("epoch date (day 0)", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DATE(1970, 1, 1));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0");
}

TEST_CASE("pre-epoch date (negative day count)", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DATE(1969, 12, 31));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "-1");
}

TEST_CASE("midnight time (0 microseconds)", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::TIME(0, 0, 0, 0));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "0");
}

TEST_CASE("large decimal exercising int128 path", "[literal_conversion]") {
	// DECIMAL(38,0) with a value near the 64-bit limit
	auto lit = ConvertValueToLiteral(Value::DECIMAL(static_cast<int64_t>(999999999999999999LL), 38, 0));
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "999999999999999999");
}

TEST_CASE("large negative decimal exercising int128 sign extension", "[literal_conversion]") {
	// hugeint_t{upper=-1, lower=1} in two's complement = -(2^64 - 1) = -18446744073709551615
	auto val = Value::HUGEINT(hugeint_t(-1, 1));
	auto decimal_val = val.DefaultCastAs(LogicalType::DECIMAL(38, 0));
	auto lit = ConvertValueToLiteral(decimal_val);
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "-18446744073709551615");
}

TEST_CASE("large positive decimal needs nonzero hugeint upper word", "[literal_conversion]") {
	// hugeint_t{upper=1, lower=0} = 2^64: the high word is nonzero, so (upper << 64) | lower must preserve it.
	auto val = Value::HUGEINT(hugeint_t(1, 0));
	auto decimal_val = val.DefaultCastAs(LogicalType::DECIMAL(38, 0));
	auto lit = ConvertValueToLiteral(decimal_val);
	REQUIRE(lit.has_value());
	REQUIRE(lit->ToString() == "18446744073709551616");
}

TEST_CASE("float NaN converts to literal", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::FLOAT(std::numeric_limits<float>::quiet_NaN()));
	REQUIRE(lit.has_value());
	REQUIRE(lit->IsNaN());
	REQUIRE(std::isnan(std::get<float>(lit->value())));
}

TEST_CASE("float infinity converts to literal", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::FLOAT(std::numeric_limits<float>::infinity()));
	REQUIRE(lit.has_value());
	auto f = std::get<float>(lit->value());
	REQUIRE(std::isinf(f));
	REQUIRE(f > 0.0f);
}

TEST_CASE("double NaN converts to literal", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DOUBLE(std::numeric_limits<double>::quiet_NaN()));
	REQUIRE(lit.has_value());
	REQUIRE(lit->IsNaN());
	REQUIRE(std::isnan(std::get<double>(lit->value())));
}

TEST_CASE("double negative infinity converts to literal", "[literal_conversion]") {
	auto lit = ConvertValueToLiteral(Value::DOUBLE(-std::numeric_limits<double>::infinity()));
	REQUIRE(lit.has_value());
	auto d = std::get<double>(lit->value());
	REQUIRE(std::isinf(d));
	REQUIRE(d < 0.0);
}
