#pragma once

#include <cstdint>
#include <limits>

namespace ctr::detail {

/**
 * @brief Dense index into a store slot table.
 *
 * Identifies a Bean<T> tracking slot. Carries no generation bits: the tracking
 * invariant guarantees a slot is freed only when all handles referencing it have
 * been destroyed, making ABA impossible (see specs-internal.md §2.2, ADR-O3).
 */
using SlotId = std::uint32_t;

/** Invalid or uninitialized slot identifier. */
inline constexpr SlotId kInvalidSlotId = std::numeric_limits<SlotId>::max();

} // namespace ctr::detail
