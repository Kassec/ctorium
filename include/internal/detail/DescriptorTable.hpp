#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

#include "../Descriptor.hpp"
#include "../DescriptorId.hpp"
#include "../Lifetime.hpp"

namespace ctr::detail {

/**
 * @brief Append-only table of runtime descriptors, indexed by dense `DescriptorId`.
 *
 * Built exclusively during `start()` from submitted `ContributedDescriptor` spans.
 * Each contributed descriptor is translated into a hot runtime `Descriptor` and
 * a parallel `DescriptorCold` block (TypeId / NameId interned, `Identity`
 * stripped, `factoryMethodIdentity` resolved to a `DescriptorId`) and appended
 * here at the same dense index.
 *
 * ### Immutability after start()
 * Once `start()` completes, the table is read-only for the context's lifetime.
 * No synchronization is required on read paths after that point.
 *
 * ### Reference stability
 * `std::vector` is grown by append only, and no element is ever erased or moved
 * after `start()` seals the table.  All `const Descriptor&` references returned
 * by `at()` and all `const DescriptorCold&` references returned by `coldAt()`
 * are therefore stable for the lifetime of the table.
 *
 * ### Indexing invariant
 * `DescriptorId` equals the 0-based insertion index.  The first appended descriptor
 * receives id 0, the second id 1, and so on.
 */
class DescriptorTable {
public:
    /**
     * @brief Reserves descriptor and lifetime storage for the `start()` build phase.
     *
     * Changes only vector capacity; descriptor order, existing ids, and contents are
     * unchanged.
     *
     * @param count Expected descriptor count.
     */
    void reserve(std::size_t count) {
        entries_.reserve(count);
        coldEntries_.reserve(count);
        lifetimes_.reserve(count);
    }

    /**
     * @brief Appends a descriptor and returns its stable dense identifier.
     *
     * Must be called only during `start()`, before the table is sealed.
     * Inserting after the context is started is a programming error.
     *
     * @param d Fully translated hot runtime descriptor.  Moved into storage.
     * @param cold Fully translated cold runtime descriptor.  Moved into storage.
     * @return Dense `DescriptorId` equal to the 0-based insertion index.
     */
    [[nodiscard]] DescriptorId append(Descriptor&& d, DescriptorCold&& cold) {
        const auto id = static_cast<DescriptorId>(entries_.size());
        lifetimes_.push_back(d.lifetime);
        entries_.push_back(std::move(d));
        coldEntries_.push_back(std::move(cold));
        return id;
    }

    /**
     * @brief Returns the `Lifetime` of the descriptor at `id` without loading the full `Descriptor`.
     *
     * Used by `resolve<T>` to discriminate the lifetime on the hot path before fetching
     * the full `Descriptor` (which is deferred to the slow path for Singleton).
     *
     * @pre `id < size()` — violated id is a programming error; asserted in debug.
     */
    [[nodiscard]] Lifetime lifetimeOf(DescriptorId id) const noexcept {
        assert(static_cast<std::size_t>(id) < lifetimes_.size()
            && "DescriptorId out of range");
        return lifetimes_[static_cast<std::size_t>(id)];
    }

    /**
     * @brief Returns a mutable reference to the descriptor at `id`.
     *
     * Reserved for use during the `start()` build phase only (e.g. linking
     * aliases or assigning session slots). Must not be called after `start()`
     * seals the table.
     *
     * @pre `id < size()` — violated id is a programming error; asserted in debug.
     * @param id Valid `DescriptorId` obtained from a prior `append()` call.
     */
    [[nodiscard]] Descriptor& atMutable(DescriptorId id) noexcept {
        assert(static_cast<std::size_t>(id) < entries_.size()
            && "DescriptorId out of range");
        return entries_[static_cast<std::size_t>(id)];
    }

    /**
     * @brief Returns a mutable reference to the cold descriptor block at `id`.
     *
     * Reserved for use during the `start()` build phase only (e.g. resolving
     * `factoryMethodDescriptor` in Phase 2).  Must not be called after `start()`
     * seals the table.
     *
     * @pre `id < size()` - violated id is a programming error; asserted in debug.
     * @param id Valid `DescriptorId` obtained from a prior `append()` call.
     */
    [[nodiscard]] DescriptorCold& coldAtMutable(DescriptorId id) noexcept {
        assert(static_cast<std::size_t>(id) < coldEntries_.size()
            && "DescriptorId out of range");
        return coldEntries_[static_cast<std::size_t>(id)];
    }

    /**
     * @brief Returns an immutable, stable reference to the descriptor at `id`.
     *
     * The reference remains valid for the lifetime of the table (see class doc).
     *
     * @pre `id < size()` — violated id is a programming error; asserted in debug.
     * @param id Valid `DescriptorId` obtained from a prior `append()` call.
     */
    [[nodiscard]] const Descriptor& at(DescriptorId id) const noexcept {
        assert(static_cast<std::size_t>(id) < entries_.size()
            && "DescriptorId out of range");
        return entries_[static_cast<std::size_t>(id)];
    }

    /**
     * @brief Returns an immutable, stable reference to the cold descriptor block at `id`.
     *
     * The reference remains valid for the lifetime of the table (see class doc).
     *
     * @pre `id < size()` - violated id is a programming error; asserted in debug.
     * @param id Valid `DescriptorId` obtained from a prior `append()` call.
     */
    [[nodiscard]] const DescriptorCold& coldAt(DescriptorId id) const noexcept {
        assert(static_cast<std::size_t>(id) < coldEntries_.size()
            && "DescriptorId out of range");
        return coldEntries_[static_cast<std::size_t>(id)];
    }

    /** @brief Number of descriptors currently stored. */
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /** @brief True when no descriptors have been appended yet. */
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

private:
    std::vector<Descriptor> entries_;
    std::vector<DescriptorCold> coldEntries_;
    std::vector<Lifetime>   lifetimes_;
};

} // namespace ctr::detail
