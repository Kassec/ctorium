#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "../Descriptor.hpp"

namespace ctr::detail {

/**
 * @brief Per-scope storage for session bean instances, keyed by dense session slot.
 *
 * Owned by `ScopedContext`; created (via `resize`) at scope `start()` and
 * destroyed (via `releaseAll`) at scope `stop()`.  Memory model mirrors
 * `SingletonStore` (A4 revision): atomic base pointer + size pair, graveyard.
 *
 * ### Thread safety
 * `store()` and `growAndStore()` must be called under the registry write lock.
 * `find()` is lock-free (acquire loads on size, base, slot).
 * `releaseAll()` runs under exclusive scope ownership (`ScopedContext::stop()`).
 */
class SessionStore {
public:
    /**
     * @brief Sizes the instance array for the current scope cycle.
     *
     * Creates a fresh array of `sessionSlotCount` null atomics.
     * Called once per scope `start()`.  Safe to call repeatedly (restart).
     */
    void resize(std::size_t sessionSlotCount) {
        auto newArr = std::make_unique<std::atomic<void*>[]>(sessionSlotCount);
        auto* raw = newArr.get();
        instancesGraveyard_.push_back(std::move(newArr));
        instancesBase_.store(raw, std::memory_order_release);
        instancesSize_.store(sessionSlotCount, std::memory_order_release);
    }

    /**
     * @brief Stores a fully-constructed session instance.
     *
     * Release store: pairs with the acquire load in `find()`.
     * Must be called under the registry write lock.
     */
    void store(SessionSlot slot, DescriptorId descId, void* mem) {
        insertionOrder_.push_back(descId);
        instancesBase_.load(std::memory_order_relaxed)
            [static_cast<std::size_t>(slot)].store(mem, std::memory_order_release);
    }

    /**
     * @brief Grows the array to accommodate `slot` if needed, then stores `mem`.
     *
     * All arrays accumulate in `instancesGraveyard_` and are freed in `releaseAll()`.
     * Must be called under the registry write lock.
     */
    void growAndStore(SessionSlot slot, DescriptorId descId, void* mem) {
        const std::size_t idx     = static_cast<std::size_t>(slot);
        const std::size_t newSize = idx + 1;
        const std::size_t oldSize = instancesSize_.load(std::memory_order_relaxed);
        if (newSize > oldSize) {
            auto newArr = std::make_unique<std::atomic<void*>[]>(newSize);
            auto* oldBase = instancesBase_.load(std::memory_order_relaxed);
            for (std::size_t i = 0; i < oldSize; ++i)
                newArr[i].store(
                    oldBase[i].load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
            auto* rawNew = newArr.get();
            instancesGraveyard_.push_back(std::move(newArr));
            instancesBase_.store(rawNew, std::memory_order_release);
            instancesSize_.store(newSize, std::memory_order_release);
        }
        store(slot, descId, mem);
    }

    /**
     * @brief Returns the live instance pointer, or `nullptr` if not materialized.
     *
     * Lock-free acquire load.  Load order: size → base → slot.
     */
    [[nodiscard]] void* find(SessionSlot slot) const noexcept {
        const std::size_t size = instancesSize_.load(std::memory_order_acquire);
        if (static_cast<std::size_t>(slot) >= size) return nullptr;
        auto* base = instancesBase_.load(std::memory_order_acquire);
        if (base == nullptr) return nullptr;
        return base[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
    }

    /** @brief Insertion-order vector for reverse-order destruction at `stop()`. */
    [[nodiscard]] const std::vector<DescriptorId>& insertionOrder() const noexcept {
        return insertionOrder_;
    }

    /**
     * @brief Frees all arrays and clears insertion order.
     *
     * Called by `Registry::stopScope()` after all session beans are destroyed.
     */
    void releaseAll() noexcept {
        instancesGraveyard_.clear();
        instancesBase_.store(nullptr, std::memory_order_relaxed);
        instancesSize_.store(0, std::memory_order_relaxed);
        insertionOrder_.clear();
    }

    /** @brief True after `resize()` has been called for the current cycle. */
    [[nodiscard]] bool initialized() const noexcept {
        return instancesSize_.load(std::memory_order_relaxed) > 0;
    }

private:
    std::atomic<std::atomic<void*>*>                    instancesBase_{nullptr};
    std::atomic<std::size_t>                            instancesSize_{0};
    std::vector<std::unique_ptr<std::atomic<void*>[]>> instancesGraveyard_;
    std::vector<DescriptorId>                           insertionOrder_;
};

} // namespace ctr::detail
