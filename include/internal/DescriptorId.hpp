#pragma once

#include <cstdint>
#include <limits>

namespace ctr::detail {

/**
 * @brief Dense index into the registry descriptor table.
 */
using DescriptorId = std::uint32_t;

/** Invalid or uninitialized descriptor identifier. */
inline constexpr DescriptorId kInvalidDescriptorId = std::numeric_limits<DescriptorId>::max();

} // namespace ctr::detail
