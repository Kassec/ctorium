#pragma once

#include <cstdint>
#include <limits>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Dense index into the registry descriptor table.
 */
using DescriptorId = std::uint32_t;

/** Invalid or uninitialized descriptor identifier. */
inline constexpr DescriptorId kInvalidDescriptorId = std::numeric_limits<DescriptorId>::max();

} // namespace CTORIUM_NAMESPACE::detail
