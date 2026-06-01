#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

#include "../DescriptorId.hpp"
#include "../SlotId.hpp"
#include "../../api/ctr/Errors.hpp"
#include "TreiberFreelist.hpp"

namespace ctr::detail {

class Registry;
struct ResolutionContext;

/**
 * @brief Storage for prototype bean instances with per-slot atomic reference counting.
 *
 * ### Slot reuse  (lock-free Treiber stack)
 * The freelist head is a 64-bit value encoding `(generation << 32) | slotId`.
 * `reclaimSlot()` increments the per-slot generation counter before pushing;
 * `allocate()` CASes on the full 64-bit value → ABA impossible.
 *
 * ### Chunked storage  (A7 — race-free lock-free reads)
 * Slots are stored in fixed-size `Chunk` objects (`kChunkSize = 256` elements).
 * Chunk pointers are held in a fixed-size `std::array<std::atomic<Chunk*>, kMaxChunks>`.
 * This array itself never moves in memory.  Lock-free readers load chunk pointers with
 * `acquire`; the writer (under `growMutex_`) stores with `release`.  This eliminates the
 * data race that would occur if chunk pointers were stored in a `std::vector` (whose
 * internal buffer can move on push_back).
 *
 * `kMaxChunks = 256` → max 65536 simultaneous prototype slots; trivial overhead.
 *
 * ### Thread safety
 * `retain()` and `releaseAcquire()` are lock-free: atomic operations only.
 * `allocate()`, `reclaim()`, and `reclaimSlot()` are lock-free on the freelist hot path;
 * they acquire `growMutex_` only on the chunk-allocation growth path.
 * Teardown marks slot memory as destroyed but leaves chunks allocated while
 * prototype handles still exist.
 */
class PrototypeStore {
public:
    explicit PrototypeStore(Registry* owner) noexcept
        : registry(owner) {}

    PrototypeStore(const PrototypeStore&) = delete;
    PrototypeStore& operator=(const PrototypeStore&) = delete;

    std::atomic<bool> alive{true};
    Registry* registry = nullptr;
    std::atomic<std::uint32_t> liveSlotCount{0};

    /**
     * @brief Marks the owning registry gone and releases the registry hold.
     *
     * The store deletes itself when no live prototype slot remains. Handles that
     * outlive the registry can still decrement their slot refcount and reclaim
     * the slot without dereferencing the dead registry.
     */
    void markRegistryDeadAndReleaseHold() noexcept {
        alive.store(false, std::memory_order_release);
        tryDeleteIfUnused();
    }

    /**
     * @brief Phase 1 of two-phase prototype materialization.
     *
     * Acquires a free slot via lock-free ABA-safe Treiber pop, or allocates a new
     * chunk under `growMutex_` when the freelist is empty.
     * The slot is not yet live (refcount remains 0).
     */
    [[nodiscard]] SlotId allocate(DescriptorId descId, void* mem) {
        // Lock-free pop from ABA-safe Treiber stack.
        Chunk* poppedChunk = nullptr;
        std::size_t poppedOffset = 0;
        const SlotId freeSlot = popTreiberFreelist(
            freelistHead_,
            [this, &poppedChunk, &poppedOffset](SlotId slot)
                    -> std::atomic<std::uint32_t>& {
                poppedOffset = slot % kChunkSize;
                poppedChunk = chunks_[slot / kChunkSize].load(
                    std::memory_order_acquire);
                return poppedChunk->nextFree[poppedOffset];
            });
        if (freeSlot != kInvalidSlotId) {
            prefetchRefcountForStore(poppedChunk->refcounts[poppedOffset]);
            poppedChunk->metas[poppedOffset] = {mem, descId};
            return freeSlot;
        }
        // Freelist empty — ensure the target chunk exists, then claim the slot.
        std::lock_guard lock(growMutex_);
#ifdef CTORIUM_PROTOTYPE_MAX_SLOTS
        // C3: enforce user-configured slot cap before growing the table.
        if (slotCount_ >= static_cast<std::size_t>(CTORIUM_PROTOTYPE_MAX_SLOTS)) {
            throw ctr::ResolutionError(
                "PrototypeStore: CTORIUM_PROTOTYPE_MAX_SLOTS limit reached; "
                "cannot allocate a new prototype slot.");
        }
#endif
        const std::size_t id = slotCount_++;
        const std::size_t chunkIdx = id / kChunkSize;
        const std::size_t offset = id % kChunkSize;
        assert(chunkIdx < kMaxChunks && "PrototypeStore: exceeded kMaxChunks slots");
        Chunk* chunk = chunks_[chunkIdx].load(std::memory_order_acquire);
        if (chunk == nullptr) {
            auto owned = std::make_unique<Chunk>();
            chunk = owned.get();
            chunks_[chunkIdx].store(chunk, std::memory_order_release);
            ownedChunks_.push_back(std::move(owned));
        }
        prefetchRefcountForStore(chunk->refcounts[offset]);
        chunk->metas[offset] = {mem, descId};
        // refcounts, nextFree, generation are value-initialised to 0 by Chunk ctor.
        return static_cast<SlotId>(id);
    }

    /** @brief Sets the refcount to 1 (slot becomes live). */
    void activate(SlotId id) noexcept {
        refcountAt(id).store(1, std::memory_order_relaxed);
        liveSlotCount.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Phase 3b: failure path — returns the slot to the Treiber freelist.
     * Does NOT increment the generation counter (only reclaimSlot does).
     */
    void reclaim(SlotId id) noexcept {
        const std::size_t offset = id % kChunkSize;
        Chunk* chunk = chunks_[id / kChunkSize].load(std::memory_order_acquire);
        chunk->metas[offset].memory = nullptr;
        const std::uint32_t generation = chunk->generation[offset];
        pushTreiberFreelist(
            freelistHead_,
            id,
            generation,
            [chunk, offset](SlotId) -> std::atomic<std::uint32_t>& {
                return chunk->nextFree[offset];
            });
    }

    /** @brief Increments the reference count (Bean<T> copy). Lock-free. */
    void retain(SlotId id) noexcept {
        assert(static_cast<std::size_t>(id) < slotCount_
            && metaAt(id).memory != nullptr);
        refcountAt(id).fetch_add(1, std::memory_order_relaxed);
    }

    /** @brief Returns the raw instance pointer for the given live slot. */
    [[nodiscard]] void* pointerAt(SlotId id) const noexcept {
        assert(static_cast<std::size_t>(id) < slotCount_
            && metaAt(id).memory != nullptr);
        return metaAt(id).memory;
    }

    struct SlotMetaSnapshot {
        void*        memory;
        DescriptorId descId;
    };

    /** @brief Returns memory pointer and DescriptorId in one access. */
    [[nodiscard]] SlotMetaSnapshot slotMetaAt(SlotId id) const noexcept {
        assert(static_cast<std::size_t>(id) < slotCount_);
        const auto& m = metaAt(id);
        return {m.memory, m.descId};
    }

    /**
     * @brief Phase 1 of two-phase prototype release: decrements refcount atomically.
     *
     * @return `true` if this was the last reference.
     */
    [[nodiscard]] bool releaseAcquire(SlotId id) noexcept {
        assert(static_cast<std::size_t>(id) < slotCount_);
        const std::uint32_t prev =
            refcountAt(id).fetch_sub(1, std::memory_order_release);
        if (prev == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    /** @brief Returns the DescriptorId for the given slot. */
    [[nodiscard]] DescriptorId descriptorIdAt(SlotId id) const noexcept {
        assert(static_cast<std::size_t>(id) < slotCount_);
        return metaAt(id).descId;
    }

    /**
     * @brief Returns the raw instance pointer, or `nullptr` for freed/out-of-range slots.
     * Used by `Registry::stop()` under exclusive ownership.
     */
    [[nodiscard]] void* memoryAt(SlotId id) const noexcept {
        if (static_cast<std::size_t>(id) >= slotCount_) return nullptr;
        const Chunk* c = chunks_[id / kChunkSize].load(std::memory_order_relaxed);
        if (c == nullptr) return nullptr;
        return c->metas[id % kChunkSize].memory;
    }

    /**
     * @brief Phase 2 of two-phase prototype release: returns the slot to the freelist.
     *
     * Increments per-slot generation counter first (A3: ABA prevention).
     */
    void reclaimSlot(SlotId id) noexcept {
        const std::size_t offset = id % kChunkSize;
        Chunk* chunk = chunks_[id / kChunkSize].load(std::memory_order_acquire);
        chunk->metas[offset].memory = nullptr;
        const std::uint32_t generation = ++chunk->generation[offset];
        pushTreiberFreelist(
            freelistHead_,
            id,
            generation,
            [chunk, offset](SlotId) -> std::atomic<std::uint32_t>& {
                return chunk->nextFree[offset];
            });
        releaseLiveSlot();
    }

    /**
     * @brief Marks a slot's object as destroyed and returns its previous metadata.
     *
     * The slot remains live and is not pushed to the freelist until the last
     * prototype handle releases it. This lets teardown destroy the object without
     * freeing the storage while handles still exist.
     */
    [[nodiscard]] SlotMetaSnapshot takeSlotMetaForDestruction(SlotId id) noexcept {
        if (static_cast<std::size_t>(id) >= slotCount_)
            return {nullptr, 0};
        Chunk* chunk = chunks_[id / kChunkSize].load(std::memory_order_acquire);
        if (chunk == nullptr)
            return {nullptr, 0};
        SlotMeta& meta = chunk->metas[id % kChunkSize];
        SlotMetaSnapshot snapshot{meta.memory, meta.descId};
        meta.memory = nullptr;
        return snapshot;
    }

    /** @brief Total number of allocated slots (live + free). */
    [[nodiscard]] std::size_t slotCount() const noexcept { return slotCount_; }

private:
    struct SlotMeta {
        void*        memory = nullptr;
        DescriptorId descId = 0;
    };

    // -------------------------------------------------------------------------
    // Chunked storage  (A7 — lock-free safe)
    // -------------------------------------------------------------------------

    static constexpr std::size_t kChunkSize = 256;
    static constexpr std::size_t kMaxChunks = 256; // max 65536 slots
#ifdef __cpp_lib_hardware_interference_size
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
#endif
    static constexpr std::size_t kRefcountAlignment =
        std::hardware_destructive_interference_size;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#else
    static constexpr std::size_t kRefcountAlignment = 64;
#endif

    /**
     * @brief One hot prototype refcount isolated on its own cache line.
     *
     * Memory trade-off: each slot spends one destructive-interference line for
     * its refcount so concurrent retain/release on neighboring slots do not
     * false-share. Other chunk arrays stay compact because they are not the
     * contended per-slot hot counters.
     */
    struct alignas(kRefcountAlignment) RefcountCell {
        std::atomic<uint32_t> value{};
    };
    static_assert(alignof(RefcountCell) == kRefcountAlignment);
    static_assert(sizeof(RefcountCell) % kRefcountAlignment == 0);

    static void prefetchRefcountForStore(RefcountCell& cell) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(static_cast<const void*>(&cell.value), 1, 3);
#else
        (void)cell;
#endif
    }

    /**
     * Fixed-size chunk of kChunkSize slots.  Heap-allocated; pointer stored
     * atomically in chunks_[].  Value-initialised: atomics = 0, generation = 0.
     */
    struct Chunk {
        SlotMeta              metas[kChunkSize];
        RefcountCell          refcounts[kChunkSize];
        std::atomic<uint32_t> nextFree[kChunkSize];
        uint32_t              generation[kChunkSize];
    };

    // Chunk pointers: fixed-size atomic array — never reallocates.
    // Lock-free readers load with acquire; grower stores with release.
    std::array<std::atomic<Chunk*>, kMaxChunks> chunks_{};

    // Ownership of chunks; accessed under growMutex_ or by the store destructor.
    std::vector<std::unique_ptr<Chunk>> ownedChunks_;

    // -------------------------------------------------------------------------
    // Accessors (load chunk pointer with acquire for lock-free readers)
    // -------------------------------------------------------------------------

    SlotMeta& metaAt(std::size_t id) noexcept {
        return chunks_[id / kChunkSize].load(std::memory_order_acquire)
                   ->metas[id % kChunkSize];
    }
    const SlotMeta& metaAt(std::size_t id) const noexcept {
        return chunks_[id / kChunkSize].load(std::memory_order_acquire)
                   ->metas[id % kChunkSize];
    }
    std::atomic<uint32_t>& refcountAt(std::size_t id) noexcept {
        return chunks_[id / kChunkSize].load(std::memory_order_acquire)
                   ->refcounts[id % kChunkSize].value;
    }
    std::atomic<uint32_t>& nextFreeAt(std::size_t id) noexcept {
        return chunks_[id / kChunkSize].load(std::memory_order_acquire)
                   ->nextFree[id % kChunkSize];
    }
    uint32_t& generationAt(std::size_t id) noexcept {
        return chunks_[id / kChunkSize].load(std::memory_order_acquire)
                   ->generation[id % kChunkSize];
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    /// Total allocated slots.  Protected by growMutex_ on write;
    /// read lock-free only in stop() (exclusive) and in debug asserts (benign).
    std::size_t slotCount_ = 0;

    /// ABA-safe Treiber freelist head: `(generation << 32) | slotId`.
    /// Empty sentinel: `static_cast<uint64_t>(kInvalidSlotId)` (bits 32-63 = 0).
    std::atomic<std::uint64_t> freelistHead_{
        static_cast<std::uint64_t>(kInvalidSlotId)};

    /// Serialises chunk allocation (ownedChunks_ push_back, slotCount_ increment,
    /// chunks_[idx] release-store).  Never acquired by retain(), releaseAcquire(),
    /// or activate().
    std::mutex growMutex_;

    std::atomic<bool> deleteClaimed_{false};

    void releaseLiveSlot() noexcept {
        const std::uint32_t previous =
            liveSlotCount.fetch_sub(1, std::memory_order_acq_rel);
        assert(previous > 0);
        if (previous == 1)
            tryDeleteIfUnused();
    }

    void tryDeleteIfUnused() noexcept {
        if (liveSlotCount.load(std::memory_order_acquire) != 0)
            return;
        if (alive.load(std::memory_order_acquire))
            return;
        if (!deleteClaimed_.exchange(true, std::memory_order_acq_rel))
            delete this;
    }
};

} // namespace ctr::detail
