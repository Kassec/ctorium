#pragma once

#include <cstdint>
#include <limits>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Dense type identifier, frozen after start().
 */
using TypeId = std::uint32_t;

/** Invalid or uninitialized type identifier. */
inline constexpr TypeId kInvalidTypeId = std::numeric_limits<TypeId>::max();

} // namespace CTORIUM_NAMESPACE::detail
