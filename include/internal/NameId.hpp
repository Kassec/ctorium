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

} // namespace ctr::detail
