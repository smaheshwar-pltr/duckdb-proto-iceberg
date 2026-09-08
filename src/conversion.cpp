#include "conversion.hpp"

#include "duckdb/planner/table_filter.hpp"
#include "duckdb/planner/table_filter_set.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"
#include "duckdb/planner/filter/table_filter_functions.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include "iceberg/expression/expressions.h"
#include "iceberg/schema_field.h"
#include "iceberg/util/int128.h"

#include <numeric>
#include <ranges>

namespace duckdb::conversion {
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
			                return make_pair(Identifier(std::string(field.name())), MapIcebergType(*field.type()));
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
		return iceberg::Literal::Time(TimeValue::Get(value).value);
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

std::shared_ptr<iceberg::Expression> TranslateComparison(const std::string &column_name, ExpressionType comparison_type,
                                                         const Value &constant) {
	auto literal = ConvertValueToLiteral(constant);
	if (!literal.has_value()) {
		return iceberg::Expressions::AlwaysTrue();
	}
	switch (comparison_type) {
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

bool IsDirectReference(const Expression &expression);

template <typename T>
Value PhysicalValue(T value, const LogicalType &type) {
	return Value::CreateValue<T>(value).WithType(type);
}

template <typename T>
std::shared_ptr<iceberg::Expression> TranslatePrefixRange(const std::string &column_name,
                                                          const PrefixRangeFunctionData &data) {
	auto minimum = Value::MinimumValue(data.key_type).GetValueUnsafe<T>();
	auto maximum = Value::MaximumValue(data.key_type).GetValueUnsafe<T>();
	auto might_match = [&](T lower, T upper) {
		return data.filter->LookupRange(PhysicalValue(lower, data.key_type), PhysicalValue(upper, data.key_type)) !=
		       FilterPropagateResult::FILTER_ALWAYS_FALSE;
	};
	if (!might_match(minimum, maximum)) {
		return iceberg::Expressions::AlwaysTrue();
	}

	// LookupRange has no false negatives. False positives can widen these bounds but cannot exclude a matching value.
	auto lower = minimum;
	auto upper = maximum;
	while (lower < upper) {
		auto midpoint = std::midpoint(lower, upper);
		if (might_match(minimum, midpoint)) {
			upper = midpoint;
		} else {
			lower = static_cast<T>(midpoint + 1);
		}
	}
	auto recovered_lower = lower;

	lower = recovered_lower;
	upper = maximum;
	while (lower < upper) {
		auto midpoint = std::midpoint(lower, upper);
		if (midpoint == lower) {
			midpoint = static_cast<T>(midpoint + 1);
		}
		if (might_match(midpoint, maximum)) {
			lower = midpoint;
		} else {
			upper = static_cast<T>(midpoint - 1);
		}
	}

	return iceberg::Expressions::And(TranslateComparison(column_name, ExpressionType::COMPARE_GREATERTHANOREQUALTO,
	                                                     PhysicalValue(recovered_lower, data.key_type)),
	                                 TranslateComparison(column_name, ExpressionType::COMPARE_LESSTHANOREQUALTO,
	                                                     PhysicalValue(lower, data.key_type)));
}

std::shared_ptr<iceberg::Expression> TranslatePrefixRange(const std::string &column_name,
                                                          const BoundFunctionExpression &function) {
	if (!function.BindInfo() || function.GetChildren().size() != 1 || !IsDirectReference(*function.GetChildren()[0])) {
		return iceberg::Expressions::AlwaysTrue();
	}
	auto &data = function.BindInfo()->Cast<PrefixRangeFunctionData>();
	if (!data.filter || !data.filter->IsInitialized() || !data.filters_null_values) {
		return iceberg::Expressions::AlwaysTrue();
	}
	switch (data.key_type.InternalType()) {
	case PhysicalType::INT8:
		return TranslatePrefixRange<int8_t>(column_name, data);
	case PhysicalType::INT16:
		return TranslatePrefixRange<int16_t>(column_name, data);
	case PhysicalType::INT32:
		return TranslatePrefixRange<int32_t>(column_name, data);
	case PhysicalType::INT64:
		return TranslatePrefixRange<int64_t>(column_name, data);
	default:
		return iceberg::Expressions::AlwaysTrue();
	}
}

std::shared_ptr<iceberg::Expression> TranslateConstantFilter(const std::string &column_name,
                                                             const LegacyConstantFilter &filter) {
	return TranslateComparison(column_name, filter.comparison_type, filter.constant);
}

std::shared_ptr<iceberg::Expression> TranslateInFilter(const std::string &column_name, const LegacyInFilter &filter) {
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

std::shared_ptr<iceberg::Expression> TranslateConjunctionAnd(const LegacyConjunctionAndFilter &filter,
                                                             const iceberg::SchemaField &field) {
	return std::ranges::fold_left(filter.child_filters | std::views::transform([&](const auto &child) {
		                              return TranslateOrWidenFilter(*child, field);
	                              }),
	                              iceberg::Expressions::AlwaysTrue(), [](auto combined, auto translated) {
		                              return iceberg::Expressions::And(std::move(combined), std::move(translated));
	                              });
}

std::shared_ptr<iceberg::Expression> TranslateConjunctionOr(const LegacyConjunctionOrFilter &filter,
                                                            const iceberg::SchemaField &field) {
	return std::ranges::fold_left(filter.child_filters | std::views::transform([&](const auto &child) {
		                              return TranslateOrWidenFilter(*child, field);
	                              }),
	                              iceberg::Expressions::AlwaysFalse(), [](auto combined, auto translated) {
		                              return iceberg::Expressions::Or(std::move(combined), std::move(translated));
	                              });
}

bool IsDirectReference(const Expression &expression) {
	return expression.GetExpressionClass() == ExpressionClass::BOUND_REF ||
	       expression.GetExpressionClass() == ExpressionClass::BOUND_COLUMN_REF;
}

std::shared_ptr<iceberg::Expression> TranslateOrWidenExpression(const Expression &expression,
                                                                const iceberg::SchemaField &field) {
	auto column_name = std::string(field.name());
	if (BoundComparisonExpression::IsComparison(expression)) {
		auto &comparison = expression.Cast<BoundFunctionExpression>();
		auto &left = BoundComparisonExpression::Left(comparison);
		auto &right = BoundComparisonExpression::Right(comparison);
		if (IsDirectReference(left) && right.GetExpressionClass() == ExpressionClass::BOUND_CONSTANT) {
			return TranslateComparison(column_name, comparison.GetExpressionType(),
			                           right.Cast<BoundConstantExpression>().GetValue());
		}
		if (left.GetExpressionClass() == ExpressionClass::BOUND_CONSTANT && IsDirectReference(right)) {
			return TranslateComparison(column_name, FlipComparisonExpression(comparison.GetExpressionType()),
			                           left.Cast<BoundConstantExpression>().GetValue());
		}
		return iceberg::Expressions::AlwaysTrue();
	}

	switch (expression.GetExpressionClass()) {
	case ExpressionClass::BOUND_CONJUNCTION: {
		auto &conjunction = expression.Cast<BoundConjunctionExpression>();
		auto is_and = expression.GetExpressionType() == ExpressionType::CONJUNCTION_AND;
		if (!is_and && expression.GetExpressionType() != ExpressionType::CONJUNCTION_OR) {
			return iceberg::Expressions::AlwaysTrue();
		}
		std::shared_ptr<iceberg::Expression> combined = iceberg::Expressions::AlwaysTrue();
		if (!is_and) {
			combined = iceberg::Expressions::AlwaysFalse();
		}
		for (auto &child : conjunction.GetChildren()) {
			auto translated = TranslateOrWidenExpression(*child, field);
			combined = is_and ? iceberg::Expressions::And(std::move(combined), std::move(translated))
			                  : iceberg::Expressions::Or(std::move(combined), std::move(translated));
		}
		return combined;
	}
	case ExpressionClass::BOUND_OPERATOR: {
		auto &operator_expression = expression.Cast<BoundOperatorExpression>();
		auto &children = operator_expression.GetChildren();
		switch (expression.GetExpressionType()) {
		case ExpressionType::OPERATOR_IS_NULL:
		case ExpressionType::OPERATOR_IS_NOT_NULL:
			if (children.size() != 1 || !IsDirectReference(*children[0])) {
				return iceberg::Expressions::AlwaysTrue();
			}
			return expression.GetExpressionType() == ExpressionType::OPERATOR_IS_NULL
			           ? iceberg::Expressions::IsNull(column_name)
			           : iceberg::Expressions::NotNull(column_name);
		case ExpressionType::COMPARE_IN: {
			if (children.size() < 2 || !IsDirectReference(*children[0])) {
				return iceberg::Expressions::AlwaysTrue();
			}
			std::vector<iceberg::Literal> literals;
			literals.reserve(children.size() - 1);
			for (idx_t index = 1; index < children.size(); index++) {
				if (children[index]->GetExpressionClass() != ExpressionClass::BOUND_CONSTANT) {
					return iceberg::Expressions::AlwaysTrue();
				}
				auto literal = ConvertValueToLiteral(children[index]->Cast<BoundConstantExpression>().GetValue());
				if (!literal) {
					return iceberg::Expressions::AlwaysTrue();
				}
				literals.push_back(std::move(*literal));
			}
			return iceberg::Expressions::In(column_name, std::move(literals));
		}
		default:
			return iceberg::Expressions::AlwaysTrue();
		}
	}
	case ExpressionClass::BOUND_FUNCTION: {
		auto &function = expression.Cast<BoundFunctionExpression>();
		if (function.Function().GetName() == PrefixRangeScalarFun::NAME) {
			return TranslatePrefixRange(column_name, function);
		}
		if (function.Function().GetName() == OptionalFilterScalarFun::NAME && function.BindInfo()) {
			auto &data = function.BindInfo()->Cast<OptionalFilterFunctionData>();
			return data.child_filter_expr ? TranslateOrWidenExpression(*data.child_filter_expr, field)
			                              : iceberg::Expressions::AlwaysTrue();
		}
		if (function.Function().GetName() == SelectivityOptionalFilterScalarFun::NAME && function.BindInfo()) {
			auto &data = function.BindInfo()->Cast<SelectivityOptionalFilterFunctionData>();
			return data.child_filter_expr ? TranslateOrWidenExpression(*data.child_filter_expr, field)
			                              : iceberg::Expressions::AlwaysTrue();
		}
		return iceberg::Expressions::AlwaysTrue();
	}
	default:
		return iceberg::Expressions::AlwaysTrue();
	}
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
	for (auto &entry : filters) {
		auto column_idx = entry.GetIndex().GetIndex();
		if (column_idx >= fields.size()) {
			continue;
		}
		auto translated = TranslateOrWidenFilter(entry.Filter(), fields[column_idx]);
		combined = iceberg::Expressions::And(std::move(combined), std::move(translated));
	}
	return combined;
}

std::shared_ptr<iceberg::Expression> TranslateOrWidenFilter(const TableFilter &filter,
                                                            const iceberg::SchemaField &field) {
	auto column_name = std::string(field.name());
	switch (filter.filter_type) {
	case TableFilterType::EXPRESSION_FILTER:
		return TranslateOrWidenExpression(*filter.Cast<ExpressionFilter>().expr, field);
	case TableFilterType::LEGACY_CONSTANT_COMPARISON:
		return TranslateConstantFilter(column_name, filter.Cast<LegacyConstantFilter>());
	case TableFilterType::LEGACY_IS_NULL:
		return iceberg::Expressions::IsNull(column_name);
	case TableFilterType::LEGACY_IS_NOT_NULL:
		return iceberg::Expressions::NotNull(column_name);
	case TableFilterType::LEGACY_IN_FILTER:
		return TranslateInFilter(column_name, filter.Cast<LegacyInFilter>());
	case TableFilterType::LEGACY_CONJUNCTION_AND:
		return TranslateConjunctionAnd(filter.Cast<LegacyConjunctionAndFilter>(), field);
	case TableFilterType::LEGACY_CONJUNCTION_OR:
		return TranslateConjunctionOr(filter.Cast<LegacyConjunctionOrFilter>(), field);
	case TableFilterType::LEGACY_OPTIONAL_FILTER: {
		if (auto &optional = filter.Cast<LegacyOptionalFilter>(); optional.child_filter) {
			return TranslateOrWidenFilter(*optional.child_filter, field);
		}
		return iceberg::Expressions::AlwaysTrue();
	}
	default:
		return iceberg::Expressions::AlwaysTrue();
	}
}

} // namespace duckdb::conversion
