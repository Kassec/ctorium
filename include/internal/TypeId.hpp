#pragma once

#include <cstdint>
#include <limits>

namespace ctr::detail {

/**
 * @brief Dense type identifier, frozen after start().
 */
using TypeId = std::uint32_t;

/** Invalid or uninitialized type identifier. */
inline constexpr TypeId kInvalidTypeId = std::numeric_limits<TypeId>::max();

} // namespace ctr::detail
