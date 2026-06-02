#pragma once

#include <cstdint>
#include <limits>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Dense index into a store slot table.
 *
 * Identifies a Bean<T> tracking slot. Carries no generation bits: the tracking
 * invariant guarantees a slot is freed only when all handles referencing it have
 * been destroyed, making ABA impossible.
 */
using SlotId = std::uint32_t;

/** Invalid or uninitialized slot identifier. */
inline constexpr SlotId kInvalidSlotId = std::numeric_limits<SlotId>::max();

} // namespace CTORIUM_NAMESPACE::detail
