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
 * A singleton is materialized at most once per root context and key.  Once live,
 * its pointer is stable until `destroyAll()` is called at root shutdown.
 *
 * ### Memory model  (specs-internal §12)
 * Each instance is heap-allocated with `::operator new(size, align)` (C++17 aligned
 * allocation).  `Descriptor::size` and `Descriptor::align` supply the parameters.
 * The `construct` thunk performs `placement-new` into the block.  `destroyAll()`
 * calls the `destroy` thunk (in-place destructor) then frees the block with the
 * matching `::operator delete(ptr, size, align)`.
 *
 * The allocation strategy (heap, pool, arena) is a store implementation concern
 * (specs-internal §12) and can be replaced without touching thunk contracts.
 *
 * ### Thread safety
 * `store()` must be called under the registry write lock.
 * `find()` is lock-free after `start()` (read-only atomic array).
 * `releaseAll()` runs under exclusive root ownership (`Registry::stop()`); no concurrent access.
 *
 * ### Separation of responsibilities
 * This store manages instance pointers and insertion order only.
 * `Registry` owns the full destruction lifecycle via `executeDestructionLifecycle`.
 */
class SingletonStore {
public:
    /**
     * @brief Stores a fully-constructed singleton that was built outside the store.
     *
     * Used by the two-phase materialization path in `Registry::resolve<T>`:
     * the registry allocates and constructs the object without holding the write
     * lock, then calls this method under the lock to register the result.
     *
     * @pre `find(descId) == nullptr` — the caller must guard against duplicates.
     * @pre `mem` points to a fully-constructed object.
     * @param descId  Descriptor this singleton maps to.
     * @param mem     Pre-constructed instance (ownership transferred to this store).
     */
    void store(DescriptorId descId, void* mem) {
        assert(instances_[static_cast<std::size_t>(descId)]
                   .load(std::memory_order_relaxed) == nullptr
            && "SingletonStore: double store");
        insertionOrder_.push_back(descId);
        // Release store: publishes the constructed object to any thread that
        // subsequently reads this slot with memory_order_acquire in find().
        instances_[static_cast<std::size_t>(descId)].store(
            mem, std::memory_order_release);
    }

    /**
     * @brief Sizes the lock-free instance table.  Must be called at the end of
     * `start()` after all descriptors are interned (descriptor count frozen).
     *
     * @param descriptorCount Total number of descriptors after `start()` Phase 1.
     */
    void resize(std::size_t descriptorCount) {
        // std::vector<std::atomic<void*>>::resize() requires MoveInsertable.
        // std::atomic is not movable; use make_unique<T[]> which value-initialises
        // each element (sets each atomic to nullptr) without any move or copy.
        instances_ = std::make_unique<std::atomic<void*>[]>(descriptorCount);
        instancesSize_ = descriptorCount;
    }

    /**
     * @brief Returns a raw pointer to the materialized instance, or `nullptr`.
     *
     * Lock-free after `start()`: uses a per-slot `atomic<void*>` acquire load.
     * Pairs with the release store in `store()`.
     *
     * @param descId `DescriptorId` to look up.
     * @return Pointer to the live singleton instance, or `nullptr` if not materialized.
     */
    [[nodiscard]] void* find(DescriptorId descId) const noexcept {
        if (static_cast<std::size_t>(descId) >= instancesSize_) return nullptr;
        return instances_[static_cast<std::size_t>(descId)].load(
            std::memory_order_acquire);
    }

    /**
     * @brief Returns the insertion-order vector for reverse-order destruction by `Registry::stop()`.
     *
     * Entries are appended by `store()` in materialization order.  `Registry::stop()`
     * iterates this vector in reverse to approximate reverse construction order.
     */
    [[nodiscard]] const std::vector<DescriptorId>& insertionOrder() const noexcept {
        return insertionOrder_;
    }

    /**
     * @brief Clears insertion order without calling any destructors or freeing memory.
     *
     * Called by `Registry::stop()` after every singleton has been destroyed via
     * `executeDestructionLifecycle`. Must run under exclusive root ownership.
     */
    void releaseAll() noexcept {
        insertionOrder_.clear();
    }

    /** @brief Number of materialized singletons. */
    [[nodiscard]] std::size_t size() const noexcept { return insertionOrder_.size(); }

private:
    /// Lock-free hot-path: one atomic<void*> per descriptor, indexed by DescriptorId.
    /// Sized by resize() at end of start(); nullptr = not yet materialized.
    /// Pairs: store(release) in store() ↔ load(acquire) in find().
    /// unique_ptr<T[]> instead of vector<T> because std::atomic is not MoveInsertable
    /// (vector::resize() would require it); make_unique<T[]>(n) value-initialises all
    /// elements to nullptr without any move or copy.
    std::unique_ptr<std::atomic<void*>[]>  instances_;
    std::size_t                            instancesSize_ = 0;
    std::vector<DescriptorId>              insertionOrder_;  ///< For reverse-order destruction.
};

} // namespace ctr::detail
