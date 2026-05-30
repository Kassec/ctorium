#pragma once

#include <cstdint>
#include <limits>

namespace ctr::detail {

/**
 * @brief Dense name identifier used to index bean named qualifiers.
 */
using NameId = std::uint32_t;

/** Reserved for the unnamed key. */
inline constexpr NameId kUnnamed = 0;

/** Invalid or uninitialized name identifier. */
inline constexpr NameId kInvalidNameId = std::numeric_limits<NameId>::max();

/**
 * Sentinel value stored in Bean<T>::f2.scopeNameId for Form 3 (threadLocal) handles.
 * Distinct from kUnnamed (0), kInvalidNameId (max), and all interned NameIds (1..N).
 * NameInterning never assigns this value: it starts at 1 and is bounded by the
 * number of distinct named qualifiers in the program.
 */
inline constexpr NameId kThreadLocalSentinel = kInvalidNameId - 1;

} // namespace ctr::detail
