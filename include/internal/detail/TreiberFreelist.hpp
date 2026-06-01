#pragma once

#include <atomic>
#include <cstdint>

#include "../SlotId.hpp"

namespace ctr::detail {

/**
 * @brief Pops one slot from an ABA-safe Treiber freelist.
 *
 * The caller owns the packed head `(generation << 32) | slot` and the slot
 * storage. `nextFreeAt(slot)` must return the atomic next-free field for that
 * slot. Empty freelists use `kInvalidSlotId` as the low 32-bit sentinel.
 */
template <class NextFreeAt>
[[nodiscard]] inline SlotId popTreiberFreelist(
        std::atomic<std::uint64_t>& head,
        NextFreeAt&& nextFreeAt) noexcept {
    constexpr std::uint64_t kSlotMask = 0xFFFF'FFFFull;

    std::uint64_t current = head.load(std::memory_order_acquire);
    while ((current & kSlotMask) != static_cast<std::uint64_t>(kInvalidSlotId)) {
        const auto slot = static_cast<SlotId>(current & kSlotMask);
        const std::uint32_t next =
            nextFreeAt(slot).load(std::memory_order_relaxed);
        if (head.compare_exchange_weak(
                current,
                static_cast<std::uint64_t>(next),
                std::memory_order_release,
                std::memory_order_acquire)) {
            return slot;
        }
    }
    return kInvalidSlotId;
}

/**
 * @brief Pushes one slot onto an ABA-safe Treiber freelist.
 *
 * `generation` is supplied by the caller because stores differ on when a slot
 * generation is bumped. This function only packs the already-computed value and
 * links the caller-provided next-free field.
 */
template <class NextFreeAt>
inline void pushTreiberFreelist(
        std::atomic<std::uint64_t>& head,
        SlotId slot,
        std::uint32_t generation,
        NextFreeAt&& nextFreeAt) noexcept {
    constexpr std::uint64_t kSlotMask = 0xFFFF'FFFFull;

    std::uint64_t current = head.load(std::memory_order_relaxed);
    do {
        nextFreeAt(slot).store(
            static_cast<std::uint32_t>(current & kSlotMask),
            std::memory_order_relaxed);
    } while (!head.compare_exchange_weak(
        current,
        (static_cast<std::uint64_t>(generation) << 32)
            | static_cast<std::uint64_t>(slot),
        std::memory_order_release,
        std::memory_order_relaxed));
}

} // namespace ctr::detail
