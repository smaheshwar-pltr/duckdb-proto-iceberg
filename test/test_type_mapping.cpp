#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "adapters.hpp"

#include "iceberg/type.h"
#include "iceberg/schema_field.h"

using namespace duckdb;
using namespace duckdb::adapters;

TEST_CASE("boolean", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::boolean()) == LogicalType::BOOLEAN);
}

TEST_CASE("int32", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::int32()) == LogicalType::INTEGER);
}

TEST_CASE("int64", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::int64()) == LogicalType::BIGINT);
}

TEST_CASE("float32", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::float32()) == LogicalType::FLOAT);
}

TEST_CASE("float64", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::float64()) == LogicalType::DOUBLE);
}

TEST_CASE("string", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::string()) == LogicalType::VARCHAR);
}

TEST_CASE("binary", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::binary()) == LogicalType::BLOB);
}

TEST_CASE("date", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::date()) == LogicalType::DATE);
}

TEST_CASE("time", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::time()) == LogicalType::TIME);
}

TEST_CASE("timestamp", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::timestamp()) == LogicalType::TIMESTAMP);
}

TEST_CASE("timestamp_tz", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::timestamp_tz()) == LogicalType::TIMESTAMP_TZ);
}

TEST_CASE("uuid", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::uuid()) == LogicalType::UUID);
}

TEST_CASE("fixed(16)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::fixed(16)) == LogicalType::BLOB);
}

TEST_CASE("fixed(1)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::fixed(1)) == LogicalType::BLOB);
}

TEST_CASE("decimal(10,2)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::decimal(10, 2)) == LogicalType::DECIMAL(10, 2));
}

TEST_CASE("decimal(38,0)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::decimal(38, 0)) == LogicalType::DECIMAL(38, 0));
}

TEST_CASE("decimal(5,5)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::decimal(5, 5)) == LogicalType::DECIMAL(5, 5));
}

TEST_CASE("decimal(1,0)", "[type_mapping]") {
	REQUIRE(MapIcebergType(*iceberg::decimal(1, 0)) == LogicalType::DECIMAL(1, 0));
}

TEST_CASE("list of integers", "[type_mapping]") {
	auto int_elem = iceberg::SchemaField::MakeOptional(1, "element", iceberg::int32());
	REQUIRE(MapIcebergType(*iceberg::list(int_elem)) == LogicalType::LIST(LogicalType::INTEGER));
}

TEST_CASE("list of strings", "[type_mapping]") {
	auto str_elem = iceberg::SchemaField::MakeOptional(1, "element", iceberg::string());
	REQUIRE(MapIcebergType(*iceberg::list(str_elem)) == LogicalType::LIST(LogicalType::VARCHAR));
}

TEST_CASE("map string to integer", "[type_mapping]") {
	auto key = iceberg::SchemaField::MakeRequired(1, "key", iceberg::string());
	auto value = iceberg::SchemaField::MakeOptional(2, "value", iceberg::int32());
	REQUIRE(MapIcebergType(*iceberg::map(key, value)) == LogicalType::MAP(LogicalType::VARCHAR, LogicalType::INTEGER));
}

TEST_CASE("struct with named fields", "[type_mapping]") {
	auto f1 = iceberg::SchemaField::MakeOptional(1, "x", iceberg::int32());
	auto f2 = iceberg::SchemaField::MakeOptional(2, "y", iceberg::string());
	auto struct_t = iceberg::struct_({f1, f2});
	auto result = MapIcebergType(*struct_t);
	REQUIRE(result == LogicalType::STRUCT({{"x", LogicalType::INTEGER}, {"y", LogicalType::VARCHAR}}));
}

TEST_CASE("list of lists", "[type_mapping]") {
	auto inner_elem = iceberg::SchemaField::MakeOptional(1, "element", iceberg::int32());
	auto inner_list = iceberg::list(inner_elem);
	auto outer_elem = iceberg::SchemaField::MakeOptional(2, "element", inner_list);
	auto outer_list = iceberg::list(outer_elem);
	REQUIRE(MapIcebergType(*outer_list) == LogicalType::LIST(LogicalType::LIST(LogicalType::INTEGER)));
}

TEST_CASE("map with struct values", "[type_mapping]") {
	auto sf1 = iceberg::SchemaField::MakeOptional(10, "a", iceberg::int32());
	auto sf2 = iceberg::SchemaField::MakeOptional(11, "b", iceberg::string());
	auto val_struct = iceberg::struct_({sf1, sf2});

	auto key = iceberg::SchemaField::MakeRequired(1, "key", iceberg::string());
	auto value = iceberg::SchemaField::MakeOptional(2, "value", val_struct);
	auto map_type = iceberg::map(key, value);

	auto expected_struct = LogicalType::STRUCT({{"a", LogicalType::INTEGER}, {"b", LogicalType::VARCHAR}});
	REQUIRE(MapIcebergType(*map_type) == LogicalType::MAP(LogicalType::VARCHAR, expected_struct));
}

TEST_CASE("list of map of string to struct", "[type_mapping]") {
	auto sf = iceberg::SchemaField::MakeOptional(20, "val", iceberg::float64());
	auto inner_struct = iceberg::struct_({sf});

	auto map_key = iceberg::SchemaField::MakeRequired(10, "key", iceberg::string());
	auto map_val = iceberg::SchemaField::MakeOptional(11, "value", inner_struct);
	auto inner_map = iceberg::map(map_key, map_val);

	auto list_elem = iceberg::SchemaField::MakeOptional(1, "element", inner_map);
	auto outer_list = iceberg::list(list_elem);

	auto expected_struct = LogicalType::STRUCT({{"val", LogicalType::DOUBLE}});
	auto expected_map = LogicalType::MAP(LogicalType::VARCHAR, expected_struct);
	REQUIRE(MapIcebergType(*outer_list) == LogicalType::LIST(expected_map));
}

TEST_CASE("struct containing list and map fields", "[type_mapping]") {
	auto list_elem = iceberg::SchemaField::MakeOptional(10, "element", iceberg::int32());
	auto list_type = iceberg::list(list_elem);

	auto map_key = iceberg::SchemaField::MakeRequired(20, "key", iceberg::string());
	auto map_val = iceberg::SchemaField::MakeOptional(21, "value", iceberg::boolean());
	auto map_type = iceberg::map(map_key, map_val);

	auto f1 = iceberg::SchemaField::MakeOptional(1, "ids", list_type);
	auto f2 = iceberg::SchemaField::MakeOptional(2, "flags", map_type);
	auto f3 = iceberg::SchemaField::MakeOptional(3, "name", iceberg::string());
	auto struct_t = iceberg::struct_({f1, f2, f3});

	auto result = MapIcebergType(*struct_t);
	auto expected = LogicalType::STRUCT({{"ids", LogicalType::LIST(LogicalType::INTEGER)},
	                                     {"flags", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::BOOLEAN)},
	                                     {"name", LogicalType::VARCHAR}});
	REQUIRE(result == expected);
}

TEST_CASE("empty struct", "[type_mapping]") {
	auto struct_t = iceberg::struct_({});
	auto result = MapIcebergType(*struct_t);
	REQUIRE(result == LogicalType::STRUCT({}));
}

namespace {

class FakeType : public iceberg::PrimitiveType {
public:
	iceberg::TypeId type_id() const override {
		return static_cast<iceberg::TypeId>(9999);
	}
	std::string ToString() const override {
		return "fake";
	}

protected:
	bool Equals(const Type &) const override {
		return false;
	}
};

} // namespace

TEST_CASE("unsupported type throws", "[type_mapping]") {
	FakeType fake;
	REQUIRE_THROWS_AS(MapIcebergType(fake), InvalidInputException);
}
