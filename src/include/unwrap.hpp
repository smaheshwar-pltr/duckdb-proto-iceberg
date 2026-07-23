#pragma once

#include "duckdb/common/exception.hpp"

#include <utility>

namespace duckdb {

/// Returns the result's value, or throws `Exc` with a formatted message and the error text appended.
template <typename Exc = IOException, typename ResultT, typename... Args>
decltype(auto) UnwrapOrThrow(ResultT &&result, const string &fmt, Args &&...args) {
	if (!result.has_value()) {
		throw Exc(fmt + ": %s", std::forward<Args>(args)..., result.error().message);
	}
	return std::forward<ResultT>(result).value();
}

} // namespace duckdb
