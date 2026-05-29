#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
#include <vector>

#include "../DescriptorId.hpp"
#include "../SlotId.hpp"

namespace ctr::detail {

struct ResolutionContext;

/**
 * @brief Storage for prototype bean instances with per-slot atomic reference counting.
 *
 * Prototypes are the only lifetime whose destruction is driven by handle reference
 * counting (specs-internal §2.4, ADR-O3).  Each instance occupies a `SlotId`-indexed
 * slot carrying its memory pointer and an atomic refcount.
 *
 * ### Reference-counting protocol  (ADR-O3 §2.4)
 * ```
 * retain  (Bean<T> copy):
 *   slot.refcount.fetch_add(1, relaxed)
 *
 * release (Bean<T> destroy):
 *   prev = releaseAcquire(slot)           // fetch_sub(release) + fence(acquire) if last
 *   if prev == 1:
 *     Registry::executeDestructionLifecycle(descId, mem)
 *     reclaimSlot(slot)
 * ```
 * This is the canonical two-phase shared-ownership protocol (same as `shared_ptr`
 * semantics but without the control-block overhead).
 *
 * ### Separation of responsibilities
 * This store manages memory slots and reference counts only.
 * `Registry` owns the full destruction lifecycle: preDestroy hook, C++ destructor,
 * listener dispatch, and `::operator delete`. The store never calls destructors.
 *
 * ### Data layout
 * Slot metadata (memory pointer, descId) is stored in a `std::deque<SlotMeta>`.
 * Per-slot refcounts are stored in a `std::deque<std::atomic<uint32_t>>`.
 * Per-slot Treiber-stack next-free links are stored in a
 * `std::deque<std::atomic<uint32_t>>`.  `std::deque` is used for all three because
 * `push_back` on `std::deque` does not invalidate existing element references or
 * pointers, making lock-free concurrent reads of live slots safe while new slots
 * are being grown under `growMutex_`.
 *
 * ### Slot reuse  (Treiber stack — ADR-O6)
 * Free slots are returned to a lock-free Treiber stack (`freelistHead_`, `nextFree_`)
 * and reused by subsequent `allocate()` calls.  Growing the slot table (when the
 * freelist is empty) is serialised by `growMutex_`, which is never held by `retain()`,
 * `releaseAcquire()`, or `activate()`.
 *
 * ### ABA invariant
 * `SlotId` carries no generation tag.  ABA is safe because a slot's refcount reaches
 * zero — and `reclaimSlot` is therefore called — only after `executeDestructionLifecycle`
 * completes.  That lifecycle encompasses `preDestroy`, `~T()`, listener dispatch, and
 * `::operator delete`, a duration structurally incompatible with another thread being
 * simultaneously inside the CAS window for the same slot in `allocate()`.
 *
 * ### Thread safety
 * `retain()` and `releaseAcquire()` are lock-free: atomic operations only, no mutex.
 * `allocate()`, `reclaim()`, and `reclaimSlot()` are lock-free on the freelist hot path
 * (Treiber pop/push); they acquire `growMutex_` only on the slot-table growth path.
 * `releaseAll()` runs under exclusive root ownership (`Registry::stop()`); no concurrent access.
 */
class PrototypeStore {
public:
    /**
     * @brief Phase 1 of two-phase prototype materialization.
     *
     * Acquires a free slot via lock-free Treiber pop, or grows the slot table under
     * `growMutex_` when the freelist is empty, and records the pre-allocated memory.
     * The slot is not yet live (refcount remains 0); no instance is constructed.
     * Memory must be allocated by the caller before this call.
     *
     * @param descId Descriptor for the bean being materialised.
     * @param mem    Pre-allocated heap memory (caller owns until activate() or reclaim()).
     */
    [[nodiscard]] SlotId allocate(DescriptorId descId, void* mem) {
        // Lock-free pop from Treiber stack.
        std::uint32_t head = freelistHead_.load(std::memory_order_acquire);
        while (head != kInvalidSlotId) {
            const std::uint32_t next = nextFree_[head].load(std::memory_order_relaxed);
            if (freelistHead_.compare_exchange_weak(head, next,
                    std::memory_order_release, std::memory_order_acquire)) {
                metas_[head] = {mem, descId};
                return head;
            }
            // CAS failure reloaded head; retry.
        }
        // Freelist empty — grow under mutex.
        std::lock_guard lock(growMutex_);
        const SlotId id = static_cast<SlotId>(metas_.size());
        metas_.push_back({mem, descId});
        refcounts_.emplace_back(0);
        nextFree_.emplace_back(kInvalidSlotId);
        return id;
    }

    /**
     * @brief Phase 3a of two-phase prototype materialization (success path).
     *
     * Sets the refcount to 1. The slot becomes live after this call.
     * May be called outside any lock: the slot is not yet reachable by other threads.
     *
     * @param id SlotId returned by allocate().
     */
    void activate(SlotId id) noexcept {
        refcounts_[id].store(1, std::memory_order_relaxed);
    }

    /**
     * @brief Phase 3b of two-phase prototype materialization (failure path).
     *
     * Returns the slot to the Treiber freelist via lock-free push. The caller must
     * free the heap memory (via `::operator delete`) before calling this method.
     *
     * @param id SlotId returned by allocate().
     */
    void reclaim(SlotId id) noexcept {
        metas_[id].memory = nullptr;
        // Lock-free push onto Treiber stack.
        std::uint32_t head = freelistHead_.load(std::memory_order_relaxed);
        do {
            nextFree_[id].store(head, std::memory_order_relaxed);
        } while (!freelistHead_.compare_exchange_weak(head, id,
                    std::memory_order_release, std::memory_order_relaxed));
    }

    /**
     * @brief Increments the reference count for the given slot.
     *
     * Called by the `Bean<T>` copy constructor.  Uses `memory_order_relaxed` because
     * the slot is guaranteed live (refcount ≥ 1) before this call — the copy source
     * holds a reference, so no synchronization with the last decrement is needed here.
     * Lock-free: acquires no mutex.
     *
     * @pre Slot is live (refcount ≥ 1 at call time).
     * @param id Valid `SlotId` returned by `allocate()` after `activate()`.
     */
    void retain(SlotId id) noexcept {
        assert(id < metas_.size() && metas_[id].memory != nullptr);
        refcounts_[id].fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Returns the raw instance pointer for the given live slot.
     *
     * Used by `Bean<T>::operator->` Form 1 fast path.
     * Valid as long as the slot's refcount is > 0.
     *
     * @param id Valid, live `SlotId`.
     * @return Non-null pointer to the prototype instance.
     */
    [[nodiscard]] void* pointerAt(SlotId id) const noexcept {
        assert(id < metas_.size() && metas_[id].memory != nullptr);
        return metas_[id].memory;
    }

    struct SlotMetaSnapshot {
        void*        memory;
        DescriptorId descId;
    };

    /**
     * @brief Returns the memory pointer and DescriptorId for a slot in one access.
     *
     * Groups the two `metas_[id]` reads (`pointerAt` + `descriptorIdAt`) into a
     * single struct copy, improving locality on the prototype release path.
     * Called by `Bean<T>::releaseIfPrototype()` after `releaseAcquire()` returns true.
     *
     * @param id Valid `SlotId` (live or being reclaimed; memory must not be null).
     */
    [[nodiscard]] SlotMetaSnapshot slotMetaAt(SlotId id) const noexcept {
        assert(id < metas_.size());
        const auto& m = metas_[id];
        return {m.memory, m.descId};
    }

    /**
     * @brief Phase 1 of two-phase prototype release: decrements refcount atomically.
     *
     * Implements the ADR-O3 two-phase release protocol:
     *  1. `fetch_sub(1, release)` — publishes all prior writes to the object.
     *  2. If the previous count was 1 (now zero): `atomic_thread_fence(acquire)` to
     *     synchronize with all prior retains, then returns `true`.
     * Lock-free: acquires no mutex.
     *
     * @pre Slot is live (refcount ≥ 1 at call time).
     * @param id Valid `SlotId`.
     * @return `true` if this was the last reference and the caller must destroy the instance.
     */
    [[nodiscard]] bool releaseAcquire(SlotId id) noexcept {
        assert(id < metas_.size());
        const std::uint32_t prev =
            refcounts_[id].fetch_sub(1, std::memory_order_release);
        if (prev == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    /**
     * @brief Returns the `DescriptorId` recorded for the given slot.
     *
     * Read-only, no lock required (safe after `allocate()` and before `reclaimSlot()`).
     *
     * @param id Valid `SlotId`.
     */
    [[nodiscard]] DescriptorId descriptorIdAt(SlotId id) const noexcept {
        assert(id < metas_.size());
        return metas_[id].descId;
    }

    /**
     * @brief Returns the raw instance pointer for the given slot, or `nullptr` for freed slots.
     *
     * Non-asserting sibling of `pointerAt()` for use in `Registry::stop()` which must
     * tolerate freed/reclaimed slots (memory == nullptr) without tripping the assert.
     *
     * @param id Slot index; returns `nullptr` if out of range or if memory was reclaimed.
     */
    [[nodiscard]] void* memoryAt(SlotId id) const noexcept {
        if (static_cast<std::size_t>(id) >= metas_.size()) return nullptr;
        return metas_[id].memory;
    }

    /**
     * @brief Phase 2 of two-phase prototype release: returns the slot to the freelist.
     *
     * Must be called by the caller after `releaseAcquire()` returned `true`,
     * and only after `Registry::executeDestructionLifecycle` has freed the memory.
     * Lock-free: uses a Treiber push. Never calls any destructor or `::operator delete` —
     * the caller (Registry) is responsible for those operations.
     *
     * @param id Valid `SlotId` whose memory was freed by the caller.
     */
    void reclaimSlot(SlotId id) noexcept {
        metas_[id].memory = nullptr;
        // Lock-free push onto Treiber stack.
        std::uint32_t head = freelistHead_.load(std::memory_order_relaxed);
        do {
            nextFree_[id].store(head, std::memory_order_relaxed);
        } while (!freelistHead_.compare_exchange_weak(head, id,
                    std::memory_order_release, std::memory_order_relaxed));
    }

    /**
     * @brief Clears all slot metadata without calling any destructors or freeing memory.
     *
     * Called by `Registry::stop()` after every live slot has been destroyed via
     * `executeDestructionLifecycle`. Empties `metas_`, `refcounts_`, `nextFree_`, and
     * resets `freelistHead_`.  Must run under exclusive root ownership; no concurrent access.
     */
    void releaseAll() noexcept {
        metas_.clear();
        refcounts_.clear();
        nextFree_.clear();
        freelistHead_.store(kInvalidSlotId, std::memory_order_relaxed);
    }

    /** @brief Total number of allocated slots (live + free). */
    [[nodiscard]] std::size_t slotCount() const noexcept { return metas_.size(); }

private:
    struct SlotMeta {
        void*        memory = nullptr;
        DescriptorId descId = 0;
    };

    /// Slot metadata.  Indexed by SlotId.  `std::deque` provides address stability on
    /// push_back, allowing lock-free reads of live slots concurrent with slot-table growth.
    std::deque<SlotMeta> metas_;

    /// Per-slot atomic refcounts.  `std::deque` preserves pointer/reference stability on
    /// push_back (std::atomic is non-movable).
    /// Cache-line padding (alignas(64)) may be reintroduced only if a benchmark
    /// demonstrates measurable false-sharing contention; the benchmark reference is
    /// required in the comment at that point.
    std::deque<std::atomic<std::uint32_t>> refcounts_;

    /// Per-slot Treiber-stack next-free links.  `nextFree_[i]` holds the index of the
    /// next free slot in the stack when slot `i` is in the freelist, or `kInvalidSlotId`.
    std::deque<std::atomic<std::uint32_t>> nextFree_;

    /// Head of the Treiber freelist.  `kInvalidSlotId` when the freelist is empty.
    std::atomic<std::uint32_t> freelistHead_{kInvalidSlotId};

    /// Serialises slot-table growth (push_back on metas_, refcounts_, nextFree_).
    /// Never acquired by retain(), releaseAcquire(), or activate().
    std::mutex growMutex_;
};

} // namespace ctr::detail
