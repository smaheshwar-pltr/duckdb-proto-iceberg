#pragma once

#include "duckdb/common/exception.hpp"

#include <type_traits>
#include <utility>

namespace duckdb {

/// Returns the result's value, or throws `Exc` with a formatted message and the error text appended. The value is
/// returned by reference for an lvalue result and moved out by value for an rvalue one, so it never refers into a
/// temporary result.
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
