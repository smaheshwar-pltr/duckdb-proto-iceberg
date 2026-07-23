#include "catch.hpp"
#include "adapters.hpp"

#include "duckdb/common/multi_file/multi_file_data.hpp"
#include "duckdb/parser/column_definition.hpp"

#include "iceberg/schema_field.h"
#include "iceberg/type.h"

using namespace duckdb;
using namespace duckdb::adapters;

namespace {

ColumnDefinition CatalogColumnFor(const iceberg::SchemaField &field) {
	ColumnDefinition col(string(field.name()), MapIcebergType(*field.type()));
	if (!field.doc().empty()) {
		col.SetComment(Value(string(field.doc())));
	}
	return col;
}

} // namespace

TEST_CASE("primitive field", "[column_definition]") {
	auto field = iceberg::SchemaField::MakeOptional(7, "age", iceberg::int32());
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "age");
	REQUIRE(col.type == LogicalType::INTEGER);
	REQUIRE(col.identifier == Value::INTEGER(7));
	REQUIRE(col.children.empty());
	// default_expression is load-bearing: a field absent from a data file reads back as its Iceberg default.
	REQUIRE(col.default_expression != nullptr);
}

TEST_CASE("primitive string field", "[column_definition]") {
	auto field = iceberg::SchemaField::MakeOptional(42, "label", iceberg::string());
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "label");
	REQUIRE(col.type == LogicalType::VARCHAR);
	REQUIRE(col.identifier == Value::INTEGER(42));
	REQUIRE(col.children.empty());
}

TEST_CASE("struct children get field IDs", "[column_definition]") {
	auto f1 = iceberg::SchemaField::MakeOptional(10, "name", iceberg::string());
	auto f2 = iceberg::SchemaField::MakeOptional(11, "score", iceberg::int32());
	auto struct_type = iceberg::struct_({f1, f2});

	auto field = iceberg::SchemaField::MakeOptional(5, "info", struct_type);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "info");
	REQUIRE(col.identifier == Value::INTEGER(5));
	REQUIRE(col.children.size() == 2);
	REQUIRE(col.default_expression != nullptr);

	REQUIRE(col.children[0].name == "name");
	REQUIRE(col.children[0].type == LogicalType::VARCHAR);
	REQUIRE(col.children[0].identifier == Value::INTEGER(10));
	REQUIRE(col.children[0].children.empty());
	REQUIRE(col.children[0].default_expression != nullptr);

	REQUIRE(col.children[1].name == "score");
	REQUIRE(col.children[1].type == LogicalType::INTEGER);
	REQUIRE(col.children[1].identifier == Value::INTEGER(11));
	REQUIRE(col.children[1].children.empty());
	REQUIRE(col.children[1].default_expression != nullptr);
}

TEST_CASE("list element gets field ID", "[column_definition]") {
	auto elem = iceberg::SchemaField::MakeOptional(20, "element", iceberg::string());
	auto list_type = iceberg::list(elem);

	auto field = iceberg::SchemaField::MakeOptional(3, "tags", list_type);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "tags");
	REQUIRE(col.type == LogicalType::LIST(LogicalType::VARCHAR));
	REQUIRE(col.identifier == Value::INTEGER(3));
	REQUIRE(col.children.size() == 1);
	REQUIRE(col.default_expression != nullptr);

	REQUIRE(col.children[0].name == "element");
	REQUIRE(col.children[0].type == LogicalType::VARCHAR);
	REQUIRE(col.children[0].identifier == Value::INTEGER(20));
	REQUIRE(col.children[0].children.empty());
	REQUIRE(col.children[0].default_expression != nullptr);
}

TEST_CASE("map key and value get field IDs", "[column_definition]") {
	auto key = iceberg::SchemaField::MakeRequired(30, "key", iceberg::string());
	auto val = iceberg::SchemaField::MakeOptional(31, "value", iceberg::int32());
	auto map_type = iceberg::map(key, val);

	auto field = iceberg::SchemaField::MakeOptional(4, "metadata", map_type);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "metadata");
	REQUIRE(col.type == LogicalType::MAP(LogicalType::VARCHAR, LogicalType::INTEGER));
	REQUIRE(col.identifier == Value::INTEGER(4));
	REQUIRE(col.children.size() == 2);
	REQUIRE(col.default_expression != nullptr);

	REQUIRE(col.children[0].name == "key");
	REQUIRE(col.children[0].type == LogicalType::VARCHAR);
	REQUIRE(col.children[0].identifier == Value::INTEGER(30));
	REQUIRE(col.children[0].children.empty());
	REQUIRE(col.children[0].default_expression != nullptr);

	REQUIRE(col.children[1].name == "value");
	REQUIRE(col.children[1].type == LogicalType::INTEGER);
	REQUIRE(col.children[1].identifier == Value::INTEGER(31));
	REQUIRE(col.children[1].children.empty());
	REQUIRE(col.children[1].default_expression != nullptr);
}

TEST_CASE("nested STRUCT(LIST(STRUCT))", "[column_definition]") {
	auto inner_f1 = iceberg::SchemaField::MakeOptional(100, "x", iceberg::int32());
	auto inner_f2 = iceberg::SchemaField::MakeOptional(101, "y", iceberg::string());
	auto inner_struct = iceberg::struct_({inner_f1, inner_f2});

	auto list_elem = iceberg::SchemaField::MakeOptional(50, "element", inner_struct);
	auto list_type = iceberg::list(list_elem);

	auto outer_f1 = iceberg::SchemaField::MakeOptional(10, "items", list_type);
	auto outer_struct = iceberg::struct_({outer_f1});

	auto field = iceberg::SchemaField::MakeOptional(1, "data", outer_struct);
	auto col = BuildColumnDefinition(field);

	// top level
	REQUIRE(col.name == "data");
	REQUIRE(col.identifier == Value::INTEGER(1));
	REQUIRE(col.children.size() == 1);

	// items (list)
	auto &items = col.children[0];
	REQUIRE(items.name == "items");
	REQUIRE(items.identifier == Value::INTEGER(10));
	REQUIRE(items.children.size() == 1);

	// element (struct)
	auto &elem = items.children[0];
	REQUIRE(elem.name == "element");
	REQUIRE(elem.identifier == Value::INTEGER(50));
	REQUIRE(elem.children.size() == 2);

	// inner struct fields
	REQUIRE(elem.children[0].name == "x");
	REQUIRE(elem.children[0].type == LogicalType::INTEGER);
	REQUIRE(elem.children[0].identifier == Value::INTEGER(100));
	REQUIRE(elem.children[0].children.empty());

	REQUIRE(elem.children[1].name == "y");
	REQUIRE(elem.children[1].type == LogicalType::VARCHAR);
	REQUIRE(elem.children[1].identifier == Value::INTEGER(101));
	REQUIRE(elem.children[1].children.empty());
}

TEST_CASE("empty struct field", "[column_definition]") {
	auto empty_struct = iceberg::struct_({});
	auto field = iceberg::SchemaField::MakeOptional(99, "empty", empty_struct);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "empty");
	REQUIRE(col.identifier == Value::INTEGER(99));
	REQUIRE(col.children.empty());
}

TEST_CASE("map with struct value children", "[column_definition]") {
	auto sf1 = iceberg::SchemaField::MakeOptional(200, "a", iceberg::int32());
	auto sf2 = iceberg::SchemaField::MakeOptional(201, "b", iceberg::string());
	auto val_struct = iceberg::struct_({sf1, sf2});

	auto key = iceberg::SchemaField::MakeRequired(60, "key", iceberg::string());
	auto val = iceberg::SchemaField::MakeOptional(61, "value", val_struct);
	auto map_type = iceberg::map(key, val);

	auto field = iceberg::SchemaField::MakeOptional(2, "props", map_type);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "props");
	REQUIRE(col.identifier == Value::INTEGER(2));
	REQUIRE(col.children.size() == 2);

	// key
	REQUIRE(col.children[0].name == "key");
	REQUIRE(col.children[0].identifier == Value::INTEGER(60));
	REQUIRE(col.children[0].children.empty());

	// value (struct)
	auto &val_child = col.children[1];
	REQUIRE(val_child.name == "value");
	REQUIRE(val_child.identifier == Value::INTEGER(61));
	REQUIRE(val_child.children.size() == 2);

	REQUIRE(val_child.children[0].name == "a");
	REQUIRE(val_child.children[0].type == LogicalType::INTEGER);
	REQUIRE(val_child.children[0].identifier == Value::INTEGER(200));

	REQUIRE(val_child.children[1].name == "b");
	REQUIRE(val_child.children[1].type == LogicalType::VARCHAR);
	REQUIRE(val_child.children[1].identifier == Value::INTEGER(201));
}

TEST_CASE("field doc maps to catalog column comment", "[column_definition]") {
	auto documented = iceberg::SchemaField::MakeOptional(1, "amount", iceberg::int32(), "dollars owed");
	auto col = CatalogColumnFor(documented);
	REQUIRE(!col.Comment().IsNull());
	REQUIRE(col.Comment() == Value("dollars owed"));
}

TEST_CASE("absent field doc leaves catalog column comment NULL", "[column_definition]") {
	auto undocumented = iceberg::SchemaField::MakeOptional(1, "amount", iceberg::int32());
	auto col = CatalogColumnFor(undocumented);
	REQUIRE(col.Comment().IsNull());
}

TEST_CASE("nested lists", "[column_definition]") {
	auto inner_elem = iceberg::SchemaField::MakeOptional(80, "element", iceberg::int32());
	auto inner_list = iceberg::list(inner_elem);

	auto outer_elem = iceberg::SchemaField::MakeOptional(70, "element", inner_list);
	auto outer_list = iceberg::list(outer_elem);

	auto field = iceberg::SchemaField::MakeOptional(6, "matrix", outer_list);
	auto col = BuildColumnDefinition(field);

	REQUIRE(col.name == "matrix");
	REQUIRE(col.identifier == Value::INTEGER(6));
	REQUIRE(col.children.size() == 1);

	// outer element (inner list)
	auto &outer = col.children[0];
	REQUIRE(outer.name == "element");
	REQUIRE(outer.identifier == Value::INTEGER(70));
	REQUIRE(outer.children.size() == 1);

	// inner element (int)
	auto &inner = outer.children[0];
	REQUIRE(inner.name == "element");
	REQUIRE(inner.identifier == Value::INTEGER(80));
	REQUIRE(inner.type == LogicalType::INTEGER);
	REQUIRE(inner.children.empty());
}
