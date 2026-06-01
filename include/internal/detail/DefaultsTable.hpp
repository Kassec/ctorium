#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>

#include "../NameId.hpp"
#include "../TypeId.hpp"

namespace ctr::detail {
    /**
     * @brief Per-type runtime default `NameId` store with thread-safe snapshot semantics.
     *
     * Implements the `defaultNamed<T>("x")` runtime-default contract with snapshot
     * threading semantics: a resolution observes either the previous default or the
     * new one.
     *
     * ### Storage model
     * One `std::atomic<NameId>` per TypeId known at `start()`, indexed by TypeId
     * value. The array is allocated once during `start()` and never reallocated
     * afterwards. Post-start overflow TypeIds have no slot and therefore no default.
     *
     * `std::unique_ptr<std::atomic<NameId>[]>` is used instead of
     * `std::vector<std::atomic<NameId>>` because std::atomic is neither copyable nor
     * movable — vector::reserve()/resize() would require MoveInsertable.
     * make_unique<T[]> value-initialises all elements (sets each to 0 = kUnnamed).
     *
     * `kUnnamed = 0` represents "no default set."  When the stored value is `kUnnamed`,
     * `resolve<T>()` resolves from the unnamed key as if no default were configured.
     *
     * ### Thread safety
     * `resize()` runs during `start()` under the start lock.  After `start()`, the
     * array size is fixed.  `setDefault()` and `getDefault()` both use
     * `memory_order_relaxed`: `NameId` is a scalar value with no dependent heap data
     * published through it, so no release/acquire synchronization is needed for
     * snapshot semantics.
     */
    class DefaultsTable {
    public:
        /**
         * @brief Allocates one atomic slot per startup TypeId, initialized to `kUnnamed`.
         *
         * Must be called during `start()` after startup TypeIds have been assigned,
         * before any `setDefault()` call.
         *
         * @param typeCount Total number of startup TypeIds at end of `start()`.
         */
        void resize(std::size_t typeCount) {
            defaults_ = std::make_unique<std::atomic<NameId>[]>(typeCount);
            size_ = typeCount;
        }

        /**
         * @brief Sets the default `NameId` for the given `TypeId`.
         *
         * Pass `kUnnamed` to clear a previously set default.
         * Thread-safe after `resize()` has been called.
         * Uses `memory_order_relaxed`: `NameId` is a scalar with no dependent data,
         * so no release/acquire pair is needed.
         *
         * @pre `typeId < size()`.
         * @param typeId TypeId of the type being configured.
         * @param nameId NameId to use as default; `kUnnamed` to clear.
         */
        void setDefault(TypeId typeId, NameId nameId) noexcept {
            assert(static_cast<std::size_t>(typeId) < size_);
            defaults_[static_cast<std::size_t>(typeId)]
                .store(nameId, std::memory_order_relaxed);
        }

        /**
         * @brief Returns the current default `NameId` for the given `TypeId`.
         *
         * Returns `kUnnamed` when no default is set or when `typeId` is out of range.
         * Thread-safe; snapshot semantics are preserved — a resolution observes either
         * the previous or new default.  `memory_order_relaxed` suffices because no
         * heap-allocated data structure is published through the `NameId` value itself.
         *
         * @param typeId TypeId of the type being queried.
         * @return Current default `NameId`, or `kUnnamed` if none.
         */
        [[nodiscard]] NameId getDefault(TypeId typeId) const noexcept {
            if (static_cast<std::size_t>(typeId) >= size_)
                return kUnnamed;
            return defaults_[static_cast<std::size_t>(typeId)]
                .load(std::memory_order_relaxed);
        }

        /** @brief Number of TypeId slots (equals the TypeId count passed to `resize()`). */
        [[nodiscard]] std::size_t size() const noexcept { return size_; }

    private:
        std::unique_ptr<std::atomic<NameId>[]> defaults_;
        std::size_t size_ = 0;
    };
} // namespace ctr::detail
