#pragma once

#include "duckdb/common/exception.hpp"

#include <type_traits>
#include <utility>

namespace duckdb {

/// Returns the result's value, or throws Exc with the formatted message and the error appended. An rvalue result's
/// value is moved out, so the return never refers into a temporary.
template <typename Exc = IOException, typename ResultT, typename... Args>
auto UnwrapOrThrow(ResultT &&result, const string &fmt, Args &&...args)
    -> std::conditional_t<std::is_lvalue_reference_v<ResultT>, decltype(result.value()),
                          typename std::remove_cvref_t<ResultT>::value_type> {
	if (!result.has_value()) {
		throw Exc(fmt + ": %s", std::forward<Args>(args)..., result.error().message);
	}
	return std::forward<ResultT>(result).value();
}

} // namespace duckdb
