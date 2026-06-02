#pragma once

#include <cstdint>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief 64-bit data-based hash. Computed consteval at discover. Consumed at start() merge then discarded.
 */
using Identity = std::uint64_t;

/** Sentinel value: descriptor is not produced by a factory method. */
inline constexpr Identity kNoFactoryMethod = 0;

} // namespace CTORIUM_NAMESPACE::detail
