#include "adapters.hpp"

#include "duckdb/planner/table_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include "iceberg/expression/expressions.h"
#include "iceberg/schema_field.h"
#include "iceberg/util/int128.h"

#include <ranges>

namespace duckdb::adapters {
namespace {

void PopulateChildFieldIds(MultiFileColumnDefinition &col_def, const iceberg::Type &type) {
	switch (type.type_id()) {
	case iceberg::TypeId::kStruct: {
		const auto &struct_type = static_cast<const iceberg::StructType &>(type);
		for (const auto &child : struct_type.fields()) {
			col_def.children.push_back(BuildColumnDefinition(child));
		}
		break;
	}
	case iceberg::TypeId::kList: {
		const auto &list_type = static_cast<const iceberg::ListType &>(type);
		col_def.children.push_back(BuildColumnDefinition(list_type.element()));
		break;
	}
	case iceberg::TypeId::kMap: {
		const auto &map_type = static_cast<const iceberg::MapType &>(type);
		col_def.children.push_back(BuildColumnDefinition(map_type.key()));
		col_def.children.push_back(BuildColumnDefinition(map_type.value()));
		break;
	}
	default:
		break;
	}
}

} // namespace

MultiFileColumnDefinition BuildColumnDefinition(const iceberg::SchemaField &field) {
	auto duckdb_type = MapIcebergType(*field.type());
	MultiFileColumnDefinition col_def(std::string(field.name()), duckdb_type);
	col_def.identifier = Value::INTEGER(field.field_id());
	col_def.default_expression = make_uniq<ConstantExpression>(Value(std::move(duckdb_type)));
	PopulateChildFieldIds(col_def, *field.type());
	return col_def;
}

vector<MultiFileColumnDefinition> BuildColumnList(const iceberg::Schema &schema) {
	return schema.fields() | std::views::transform(BuildColumnDefinition) |
	       std::ranges::to<vector<MultiFileColumnDefinition>>();
}

LogicalType MapIcebergType(const iceberg::Type &type) {
	switch (type.type_id()) {
	case iceberg::TypeId::kBoolean:
		return LogicalType::BOOLEAN;
	case iceberg::TypeId::kInt:
		return LogicalType::INTEGER;
	case iceberg::TypeId::kLong:
		return LogicalType::BIGINT;
	case iceberg::TypeId::kFloat:
		return LogicalType::FLOAT;
	case iceberg::TypeId::kDouble:
		return LogicalType::DOUBLE;
	case iceberg::TypeId::kString:
		return LogicalType::VARCHAR;
	case iceberg::TypeId::kBinary:
		return LogicalType::BLOB;
	case iceberg::TypeId::kDate:
		return LogicalType::DATE;
	case iceberg::TypeId::kTime:
		return LogicalType::TIME;
	case iceberg::TypeId::kTimestamp:
		return LogicalType::TIMESTAMP;
	case iceberg::TypeId::kTimestampTz:
		return LogicalType::TIMESTAMP_TZ;
	case iceberg::TypeId::kUuid:
		return LogicalType::UUID;
	case iceberg::TypeId::kFixed:
		return LogicalType::BLOB;
	case iceberg::TypeId::kDecimal: {
		auto &decimal = static_cast<const iceberg::DecimalType &>(type);
		return LogicalType::DECIMAL(decimal.precision(), decimal.scale());
	}
	case iceberg::TypeId::kList: {
		auto &list_type = static_cast<const iceberg::ListType &>(type);
		return LogicalType::LIST(MapIcebergType(*list_type.element().type()));
	}
	case iceberg::TypeId::kMap: {
		auto &map_type = static_cast<const iceberg::MapType &>(type);
		return LogicalType::MAP(MapIcebergType(*map_type.key().type()), MapIcebergType(*map_type.value().type()));
	}
	case iceberg::TypeId::kStruct: {
		auto &struct_type = static_cast<const iceberg::StructType &>(type);
		auto children = struct_type.fields() | std::views::transform([](const auto &field) {
			                return make_pair(std::string(field.name()), MapIcebergType(*field.type()));
		                }) |
		                std::ranges::to<child_list_t<LogicalType>>();
		return LogicalType::STRUCT(children);
	}
	default:
		// TODO: Iceberg V3 types, see https://github.com/duckdb/duckdb-iceberg/issues/1019
		throw InvalidInputException("Unsupported Iceberg type: %s", type.ToString());
	}
}

std::optional<iceberg::Literal> ConvertValueToLiteral(const Value &value) {
	if (value.IsNull()) {
		return std::nullopt;
	}
	switch (value.type().id()) {
	case LogicalTypeId::BOOLEAN:
		return iceberg::Literal::Boolean(BooleanValue::Get(value));
	case LogicalTypeId::TINYINT:
		return iceberg::Literal::Int(TinyIntValue::Get(value));
	case LogicalTypeId::SMALLINT:
		return iceberg::Literal::Int(SmallIntValue::Get(value));
	case LogicalTypeId::INTEGER:
		return iceberg::Literal::Int(IntegerValue::Get(value));
	case LogicalTypeId::BIGINT:
		return iceberg::Literal::Long(BigIntValue::Get(value));
	case LogicalTypeId::FLOAT:
		return iceberg::Literal::Float(FloatValue::Get(value));
	case LogicalTypeId::DOUBLE:
		return iceberg::Literal::Double(DoubleValue::Get(value));
	case LogicalTypeId::VARCHAR:
		return iceberg::Literal::String(StringValue::Get(value));
	case LogicalTypeId::DATE:
		// DuckDB date_t stores days since epoch as int32_t (same as Iceberg)
		return iceberg::Literal::Date(DateValue::Get(value).days);
	case LogicalTypeId::TIME:
		// DuckDB dtime_t stores microseconds from midnight as int64_t (same as Iceberg)
		return iceberg::Literal::Time(TimeValue::Get(value).micros);
	case LogicalTypeId::TIMESTAMP:
		// DuckDB timestamp_t stores microseconds since epoch as int64_t (same as Iceberg)
		return iceberg::Literal::Timestamp(TimestampValue::Get(value).value);
	case LogicalTypeId::TIMESTAMP_TZ:
		return iceberg::Literal::TimestampTz(TimestampTZValue::Get(value).value);
	case LogicalTypeId::DECIMAL: {
		// M.B. IntegralValue::Get returns the raw unscaled integer
		auto raw = IntegralValue::Get(value);
		auto val128 = (static_cast<int128_t>(raw.upper) << 64) | raw.lower;
		auto precision = DecimalType::GetWidth(value.type());
		auto scale = DecimalType::GetScale(value.type());
		return iceberg::Literal::Decimal(val128, precision, scale);
	}
	default:
		// TODO: UUID, BLOB literal conversion
		return std::nullopt;
	}
}

namespace {

std::shared_ptr<iceberg::Expression> TranslateConstantFilter(const std::string &column_name,
                                                             const ConstantFilter &filter) {
	auto literal = ConvertValueToLiteral(filter.constant);
	if (!literal.has_value()) {
		return iceberg::Expressions::AlwaysTrue();
	}
	switch (filter.comparison_type) {
	case ExpressionType::COMPARE_EQUAL:
		return iceberg::Expressions::Equal(column_name, std::move(*literal));
	case ExpressionType::COMPARE_NOTEQUAL:
		return iceberg::Expressions::NotEqual(column_name, std::move(*literal));
	case ExpressionType::COMPARE_LESSTHAN:
		return iceberg::Expressions::LessThan(column_name, std::move(*literal));
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		return iceberg::Expressions::LessThanOrEqual(column_name, std::move(*literal));
	case ExpressionType::COMPARE_GREATERTHAN:
		return iceberg::Expressions::GreaterThan(column_name, std::move(*literal));
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		return iceberg::Expressions::GreaterThanOrEqual(column_name, std::move(*literal));
	default:
		return iceberg::Expressions::AlwaysTrue();
	}
}

std::shared_ptr<iceberg::Expression> TranslateInFilter(const std::string &column_name, const InFilter &filter) {
	std::vector<iceberg::Literal> literals;
	literals.reserve(filter.values.size());
	for (auto &val : filter.values) {
		if (auto lit = ConvertValueToLiteral(val)) {
			literals.push_back(std::move(*lit));
		} else {
			return iceberg::Expressions::AlwaysTrue();
		}
	}
	D_ASSERT(!literals.empty());
	return iceberg::Expressions::In(column_name, std::move(literals));
}

std::shared_ptr<iceberg::Expression> TranslateConjunctionAnd(const ConjunctionAndFilter &filter,
                                                             const iceberg::SchemaField &field) {
	return std::ranges::fold_left(filter.child_filters | std::views::transform([&](const auto &child) {
		                              return TranslateOrWidenFilter(*child, field);
	                              }),
	                              iceberg::Expressions::AlwaysTrue(), [](auto combined, auto translated) {
		                              return iceberg::Expressions::And(std::move(combined), std::move(translated));
	                              });
}

std::shared_ptr<iceberg::Expression> TranslateConjunctionOr(const ConjunctionOrFilter &filter,
                                                            const iceberg::SchemaField &field) {
	return std::ranges::fold_left(filter.child_filters | std::views::transform([&](const auto &child) {
		                              return TranslateOrWidenFilter(*child, field);
	                              }),
	                              iceberg::Expressions::AlwaysFalse(), [](auto combined, auto translated) {
		                              return iceberg::Expressions::Or(std::move(combined), std::move(translated));
	                              });
}

} // namespace

// N.B. Widened results and sub-results must not be negated to preserve the invariant. AlwaysTrue negates to
// AlwaysFalse, which would silently exclude all files. DuckDB normalizes negation before creating TableFilters (e.g.
// NOT x>5 becomes x<=5), so wrapped NOT filters do not arise.
// N.B. We create filter expressions using column names from ProtoIcebergScanInfo::schema. iceberg-cpp's Binder::Bind()
// resolves these names against its own derived schema to obtain field IDs. The two schemas must be the same; such is
// the precondition of ProtoIcebergScanInfo::schema for this very reason.
std::shared_ptr<iceberg::Expression> TranslateOrWidenFilters(const TableFilterSet &filters,
                                                             const iceberg::Schema &schema) {
	auto fields = schema.fields();
	std::shared_ptr<iceberg::Expression> combined = iceberg::Expressions::AlwaysTrue();
	for (auto &[column_idx, filter] : filters.filters) {
		if (column_idx >= fields.size()) {
			continue;
		}
		auto translated = TranslateOrWidenFilter(*filter, fields[column_idx]);
		combined = iceberg::Expressions::And(std::move(combined), std::move(translated));
	}
	return combined;
}

std::shared_ptr<iceberg::Expression> TranslateOrWidenFilter(const TableFilter &filter,
                                                            const iceberg::SchemaField &field) {
	auto column_name = std::string(field.name());
	switch (filter.filter_type) {
	case TableFilterType::CONSTANT_COMPARISON:
		return TranslateConstantFilter(column_name, filter.Cast<ConstantFilter>());
	case TableFilterType::IS_NULL:
		return iceberg::Expressions::IsNull(column_name);
	case TableFilterType::IS_NOT_NULL:
		return iceberg::Expressions::NotNull(column_name);
	case TableFilterType::IN_FILTER:
		return TranslateInFilter(column_name, filter.Cast<InFilter>());
	case TableFilterType::CONJUNCTION_AND:
		return TranslateConjunctionAnd(filter.Cast<ConjunctionAndFilter>(), field);
	case TableFilterType::CONJUNCTION_OR:
		return TranslateConjunctionOr(filter.Cast<ConjunctionOrFilter>(), field);
	case TableFilterType::OPTIONAL_FILTER: {
		if (auto &optional = filter.Cast<OptionalFilter>(); optional.child_filter) {
			return TranslateOrWidenFilter(*optional.child_filter, field);
		}
		return iceberg::Expressions::AlwaysTrue();
	}
	default:
		return iceberg::Expressions::AlwaysTrue();
	}
}

} // namespace duckdb::adapters
