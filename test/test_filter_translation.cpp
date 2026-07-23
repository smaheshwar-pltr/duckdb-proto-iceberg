#include "catch.hpp"
#include "adapters.hpp"

#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/null_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include "iceberg/type.h"
#include "iceberg/schema.h"
#include "iceberg/schema_field.h"
#include "iceberg/expression/expression.h"

using namespace duckdb;
using namespace duckdb::adapters;

namespace {

// Field with a dummy ID.
iceberg::SchemaField MakeField(std::string_view name, std::shared_ptr<iceberg::Type> type = iceberg::int32()) {
	return iceberg::SchemaField::MakeOptional(1, name, std::move(type));
}

} // namespace

TEST_CASE("equality on integer", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_EQUAL, Value::INTEGER(42));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") == 42");
}

TEST_CASE("not-equal on integer", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_NOTEQUAL, Value::INTEGER(42));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") != 42");
}

TEST_CASE("less-than on integer", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_LESSTHAN, Value::INTEGER(10));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") < 10");
}

TEST_CASE("less-than-or-equal on integer", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_LESSTHANOREQUALTO, Value::INTEGER(10));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") <= 10");
}

TEST_CASE("greater-than on integer", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_GREATERTHAN, Value::INTEGER(99));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") > 99");
}

TEST_CASE("greater-than-or-equal on string", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_GREATERTHANOREQUALTO, Value("abc"));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col", iceberg::string()));
	REQUIRE(expr->ToString() == "ref(name=\"col\") >= \"abc\"");
}

TEST_CASE("field name comes from SchemaField", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_EQUAL, Value("hello"));
	auto expr = TranslateOrWidenFilter(filter, MakeField("name", iceberg::string()));
	REQUIRE(expr->ToString() == "ref(name=\"name\") == \"hello\"");
}

TEST_CASE("date equality", "[filter_translation]") {
	// 2024-01-01 = day 19723 since epoch
	ConstantFilter filter(ExpressionType::COMPARE_EQUAL, Value::DATE(2024, 1, 1));
	auto expr = TranslateOrWidenFilter(filter, MakeField("dt", iceberg::date()));
	REQUIRE(expr->ToString() == "ref(name=\"dt\") == 19723");
}

TEST_CASE("time greater-than", "[filter_translation]") {
	// 12:00:00 = 43200000000 microseconds from midnight
	ConstantFilter filter(ExpressionType::COMPARE_GREATERTHAN, Value::TIME(12, 0, 0, 0));
	auto expr = TranslateOrWidenFilter(filter, MakeField("t", iceberg::time()));
	REQUIRE(expr->ToString() == "ref(name=\"t\") > 43200000000");
}

TEST_CASE("timestamp less-than", "[filter_translation]") {
	// 2024-01-01T00:00:00 as microseconds since epoch
	ConstantFilter filter(ExpressionType::COMPARE_LESSTHAN, Value::TIMESTAMP(2024, 1, 1, 0, 0, 0, 0));
	auto expr = TranslateOrWidenFilter(filter, MakeField("ts", iceberg::timestamp()));
	REQUIRE(expr->ToString() == "ref(name=\"ts\") < 1704067200000000");
}

TEST_CASE("timestamp_tz equality", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_EQUAL, Value::TIMESTAMPTZ(timestamp_tz_t(1704067200000000LL)));
	auto expr = TranslateOrWidenFilter(filter, MakeField("tstz", iceberg::timestamp_tz()));
	REQUIRE(expr->ToString() == "ref(name=\"tstz\") == 1704067200000000");
}

TEST_CASE("IS NULL", "[filter_translation]") {
	IsNullFilter filter;
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "is_null(ref(name=\"col\"))");
}

TEST_CASE("IS NOT NULL", "[filter_translation]") {
	IsNotNullFilter filter;
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "not_null(ref(name=\"col\"))");
}

TEST_CASE("IN with integers", "[filter_translation]") {
	vector<Value> values = {Value::INTEGER(1), Value::INTEGER(2), Value::INTEGER(3)};
	InFilter filter(std::move(values));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") in [1, 2, 3]");
}

TEST_CASE("IN with single value", "[filter_translation]") {
	vector<Value> values = {Value::INTEGER(42)};
	InFilter filter(std::move(values));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") in [42]");
}

TEST_CASE("IN with unconvertible values widens to AlwaysTrue", "[filter_translation]") {
	vector<Value> values = {Value::BLOB("a"), Value::BLOB("b")};
	InFilter filter(std::move(values));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("unconvertible comparison value widens to AlwaysTrue", "[filter_translation]") {
	ConstantFilter filter(ExpressionType::COMPARE_EQUAL, Value::BLOB("data"));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("optional filter with null child widens to AlwaysTrue", "[filter_translation]") {
	OptionalFilter filter(nullptr);
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("optional filter recurses into its child", "[filter_translation]") {
	auto child = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(42));
	OptionalFilter filter(std::move(child));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") == 42");
}

TEST_CASE("AND drops unsupported conjunct, keeps supported", "[filter_translation]") {
	auto supported = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5));
	auto unsupported = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("data"));
	ConjunctionAndFilter filter;
	filter.child_filters.push_back(std::move(supported));
	filter.child_filters.push_back(std::move(unsupported));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") == 5");
}

TEST_CASE("AND with all unsupported children widens to AlwaysTrue", "[filter_translation]") {
	auto u1 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("a"));
	auto u2 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("b"));
	ConjunctionAndFilter filter;
	filter.child_filters.push_back(std::move(u1));
	filter.child_filters.push_back(std::move(u2));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("AND with two supported children", "[filter_translation]") {
	auto c1 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_GREATERTHAN, Value::INTEGER(0));
	auto c2 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_LESSTHAN, Value::INTEGER(100));
	ConjunctionAndFilter filter;
	filter.child_filters.push_back(std::move(c1));
	filter.child_filters.push_back(std::move(c2));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "(ref(name=\"col\") > 0 and ref(name=\"col\") < 100)");
}

TEST_CASE("OR widens when any child is unsupported", "[filter_translation]") {
	auto supported = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5));
	auto unsupported = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("data"));
	ConjunctionOrFilter filter;
	filter.child_filters.push_back(std::move(supported));
	filter.child_filters.push_back(std::move(unsupported));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("OR with all supported children", "[filter_translation]") {
	auto c1 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(1));
	auto c2 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(2));
	ConjunctionOrFilter filter;
	filter.child_filters.push_back(std::move(c1));
	filter.child_filters.push_back(std::move(c2));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "(ref(name=\"col\") == 1 or ref(name=\"col\") == 2)");
}

TEST_CASE("nested AND(OR(supported, unsupported), supported) drops the widened OR", "[filter_translation]") {
	auto or_ok = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(1));
	auto or_bad = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("x"));
	auto or_filter = make_uniq<ConjunctionOrFilter>();
	or_filter->child_filters.push_back(std::move(or_ok));
	or_filter->child_filters.push_back(std::move(or_bad));

	auto and_ok = make_uniq<ConstantFilter>(ExpressionType::COMPARE_GREATERTHAN, Value::INTEGER(10));
	ConjunctionAndFilter filter;
	filter.child_filters.push_back(std::move(or_filter));
	filter.child_filters.push_back(std::move(and_ok));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "ref(name=\"col\") > 10");
}

TEST_CASE("nested OR(AND(unsupported, supported), supported) simplifies AND and keeps both OR branches",
          "[filter_translation]") {
	auto and_bad = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("x"));
	auto and_ok = make_uniq<ConstantFilter>(ExpressionType::COMPARE_GREATERTHAN, Value::INTEGER(10));
	auto and_filter = make_uniq<ConjunctionAndFilter>();
	and_filter->child_filters.push_back(std::move(and_bad));
	and_filter->child_filters.push_back(std::move(and_ok));

	auto or_ok = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5));
	ConjunctionOrFilter filter;
	filter.child_filters.push_back(std::move(and_filter));
	filter.child_filters.push_back(std::move(or_ok));

	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "(ref(name=\"col\") > 10 or ref(name=\"col\") == 5)");
}

TEST_CASE("single column resolves from schema", "[filter_translation]") {
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(0), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5)));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32()),
	                        iceberg::SchemaField::MakeOptional(2, "name", iceberg::string())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "ref(name=\"id\") == 5");
}

TEST_CASE("multiple columns produce AND", "[filter_translation]") {
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(0), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5)));
	filter_set.PushFilter(ColumnIndex(1), make_uniq<ConstantFilter>(ExpressionType::COMPARE_GREATERTHAN, Value("abc")));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32()),
	                        iceberg::SchemaField::MakeOptional(2, "name", iceberg::string())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "(ref(name=\"id\") == 5 and ref(name=\"name\") > \"abc\")");
}

TEST_CASE("empty filter set returns AlwaysTrue", "[filter_translation]") {
	TableFilterSet filter_set;
	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("unsupported column dropped, supported kept", "[filter_translation]") {
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(0), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::BLOB("bad")));
	filter_set.PushFilter(ColumnIndex(1), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(42)));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "data", iceberg::binary()),
	                        iceberg::SchemaField::MakeOptional(2, "id", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "ref(name=\"id\") == 42");
}

TEST_CASE("IS NOT NULL resolves from schema", "[filter_translation]") {
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(0), make_uniq<IsNotNullFilter>());

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "my_col", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "not_null(ref(name=\"my_col\"))");
}

TEST_CASE("unsupported comparison op widens to AlwaysTrue", "[filter_translation]") {
	// COMPARE_DISTINCT_FROM is not one of the six comparisons we translate, so it hits the default branch and widens.
	ConstantFilter filter(ExpressionType::COMPARE_DISTINCT_FROM, Value::INTEGER(7));
	auto expr = TranslateOrWidenFilter(filter, MakeField("col"));
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("out-of-bounds column_idx is skipped, not UB", "[filter_translation]") {
	// A filter keyed at index 5 with only one field must be skipped (no fields[5] deref), but not the one at index 0.
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(0), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(5)));
	filter_set.PushFilter(ColumnIndex(5), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(9)));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "ref(name=\"id\") == 5");
}

TEST_CASE("all out-of-bounds column_idx widens to AlwaysTrue", "[filter_translation]") {
	// Every filter is keyed past the end of the schema; all are skipped, leaving the AlwaysTrue identity.
	TableFilterSet filter_set;
	filter_set.PushFilter(ColumnIndex(3), make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(1)));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "true");
}

TEST_CASE("set-level IN resolves column from schema", "[filter_translation]") {
	TableFilterSet filter_set;
	vector<Value> values = {Value::INTEGER(1), Value::INTEGER(2), Value::INTEGER(3)};
	filter_set.PushFilter(ColumnIndex(1), make_uniq<InFilter>(std::move(values)));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32()),
	                        iceberg::SchemaField::MakeOptional(2, "category", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "ref(name=\"category\") in [1, 2, 3]");
}

TEST_CASE("set-level OR resolves column from schema", "[filter_translation]") {
	TableFilterSet filter_set;
	auto c1 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(1));
	auto c2 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_EQUAL, Value::INTEGER(2));
	auto or_filter = make_uniq<ConjunctionOrFilter>();
	or_filter->child_filters.push_back(std::move(c1));
	or_filter->child_filters.push_back(std::move(c2));
	filter_set.PushFilter(ColumnIndex(0), std::move(or_filter));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "(ref(name=\"id\") == 1 or ref(name=\"id\") == 2)");
}

TEST_CASE("set-level nested AND resolves column from schema", "[filter_translation]") {
	TableFilterSet filter_set;
	auto c1 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_GREATERTHAN, Value::INTEGER(0));
	auto c2 = make_uniq<ConstantFilter>(ExpressionType::COMPARE_LESSTHAN, Value::INTEGER(100));
	auto and_filter = make_uniq<ConjunctionAndFilter>();
	and_filter->child_filters.push_back(std::move(c1));
	and_filter->child_filters.push_back(std::move(c2));
	filter_set.PushFilter(ColumnIndex(1), std::move(and_filter));

	iceberg::Schema schema({iceberg::SchemaField::MakeOptional(1, "id", iceberg::int32()),
	                        iceberg::SchemaField::MakeOptional(2, "amount", iceberg::int32())});
	auto expr = TranslateOrWidenFilters(filter_set, schema);
	REQUIRE(expr->ToString() == "(ref(name=\"amount\") > 0 and ref(name=\"amount\") < 100)");
}
