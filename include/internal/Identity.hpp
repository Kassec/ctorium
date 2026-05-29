#pragma once

#include <cstdint>

namespace ctr::detail {

/**
 * @brief 64-bit data-based hash. Computed consteval at discover. Consumed at start() merge then discarded.
 */
using Identity = std::uint64_t;

/** Sentinel value: descriptor is not produced by a factory method. */
inline constexpr Identity kNoFactoryMethod = 0;

} // namespace ctr::detail
