#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

#include "../DescriptorId.hpp"

namespace ctr::detail {

/**
 * @brief Storage for singleton bean instances, keyed by `DescriptorId`.
 *
 * ### Memory model  (A4 revision)
 * The instance array is accessed via a frozen pre-start snapshot for all
 * descriptors that existed at `start()`.  Post-start bindings that append new
 * descriptors use the atomic base/size pair:
 *   - `instancesBase_`  — `atomic<atomic<void*>*>`: raw pointer to the live array.
 *   - `instancesSize_`  — `atomic<size_t>`:         capacity of the live array.
 *
 * `growAndStore()` allocates a new array, copies existing slots, then publishes:
 *   1. store(base, release)
 *   2. store(size, release)
 *
 * On the post-start descriptor fallback path, `find()` loads size (acquire) then base (acquire):
 * because size is published after base, observing the new size guarantees
 * observing the new base. On the frozen path, the slot load remains the only
 * atomic operation. If an original descriptor is materialized after a post-start
 * growth, `store()` mirrors that slot into the frozen snapshot.
 *
 * All arrays (old and new) are pushed to `instancesGraveyard_` and freed only in
 * `releaseAll()` (called after `stop()`), ensuring no in-flight `find()` ever
 * dereferences a freed array.
 *
 * ### Thread safety
 * `store()` and `growAndStore()` must be called under the registry write lock.
 * `find()` is lock-free after `start()` (frozen path: one acquire slot load).
 * `releaseAll()` runs under exclusive root ownership; no concurrent access.
 */
class SingletonStore {
public:
    /**
     * @brief Stores a fully-constructed singleton.
     *
     * @pre `find(descId) == nullptr`.
     * @pre Called under the registry write lock.
     */
    void store(DescriptorId descId, void* mem) {
        const std::size_t idx = static_cast<std::size_t>(descId);
        auto* base = instancesBase_.load(std::memory_order_relaxed);
        assert(base[idx].load(std::memory_order_relaxed) == nullptr
            && "SingletonStore: double store");
        insertionOrder_.push_back(descId);
        base[idx].store(mem, std::memory_order_release);

        if (idx < frozenSize_ && frozenBase_ != nullptr && frozenBase_ != base) {
            assert(frozenBase_[idx].load(std::memory_order_relaxed) == nullptr
                && "SingletonStore: frozen snapshot double store");
            frozenBase_[idx].store(mem, std::memory_order_release);
        }
    }

    /**
     * @brief Grows the array to accommodate `descId` if needed, then stores `mem`.
     *
     * All arrays (including the old one) accumulate in `instancesGraveyard_` and
     * are freed atomically in `releaseAll()`.  Concurrent `find()` calls that
     * loaded the old base remain valid until `releaseAll()` runs.
     * Must be called under the registry write lock.
     */
    void growAndStore(DescriptorId descId, void* mem) {
        const std::size_t idx     = static_cast<std::size_t>(descId);
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
            // Graveyard owns the new array; old array already owned by a prior entry.
            instancesGraveyard_.push_back(std::move(newArr));
            // Publish: base first, then size (find() loads size first — see class doc).
            instancesBase_.store(rawNew, std::memory_order_release);
            instancesSize_.store(newSize, std::memory_order_release);
        }
        store(descId, mem);
    }

    /**
     * @brief Sizes the lock-free instance array.  Called at the end of `start()`.
     *
     * @param descriptorCount Total number of descriptors after `start()` Phase 1.
     */
    void resize(std::size_t descriptorCount) {
        auto newArr = std::make_unique<std::atomic<void*>[]>(descriptorCount);
        auto* raw = newArr.get();
        instancesGraveyard_.push_back(std::move(newArr));
        frozenBase_ = raw;
        frozenSize_ = descriptorCount;
        instancesBase_.store(raw, std::memory_order_release);
        instancesSize_.store(descriptorCount, std::memory_order_release);
    }

    /**
     * @brief Returns a raw pointer to the materialized instance, or `nullptr`.
     *
     * Lock-free after `start()`.  Load order: size (acquire) → base (acquire) →
     * slot (acquire).  The acquire on size synchronises with the release on size
     * in `growAndStore()`/`resize()`, guaranteeing the base is also visible.
     */
    [[nodiscard]] void* find(DescriptorId descId) const noexcept {
        const std::size_t idx = static_cast<std::size_t>(descId);
        if (idx < frozenSize_) {
            auto* frozen = frozenBase_;
            if (frozen == nullptr) return nullptr;
            return frozen[idx].load(std::memory_order_acquire);
        }
        const std::size_t size = instancesSize_.load(std::memory_order_acquire);
        if (idx >= size) return nullptr;
        auto* base = instancesBase_.load(std::memory_order_acquire);
        if (base == nullptr) return nullptr;
        return base[idx].load(std::memory_order_acquire);
    }

    /** @brief Insertion-order vector for reverse-order destruction by `stop()`. */
    [[nodiscard]] const std::vector<DescriptorId>& insertionOrder() const noexcept {
        return insertionOrder_;
    }

    /**
     * @brief Frees all arrays and clears insertion order.
     *
     * Called by `Registry::stop()` after every singleton has been destroyed.
     * Must run under exclusive root ownership.
     */
    void releaseAll() noexcept {
        instancesGraveyard_.clear();
        frozenBase_ = nullptr;
        frozenSize_ = 0;
        instancesBase_.store(nullptr, std::memory_order_relaxed);
        instancesSize_.store(0, std::memory_order_relaxed);
        insertionOrder_.clear();
    }

    /** @brief Number of materialized singletons. */
    [[nodiscard]] std::size_t size() const noexcept { return insertionOrder_.size(); }

private:
    std::atomic<std::atomic<void*>*>                    instancesBase_{nullptr};
    std::atomic<std::size_t>                            instancesSize_{0};
    std::atomic<void*>*                                 frozenBase_ = nullptr;
    std::size_t                                         frozenSize_ = 0;
    std::vector<std::unique_ptr<std::atomic<void*>[]>> instancesGraveyard_;
    std::vector<DescriptorId>                           insertionOrder_;
};

} // namespace ctr::detail
