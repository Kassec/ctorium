#pragma once

#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "../../api/ctr/Config.hpp"

#include "../TypeId.hpp"
#include "../../api/ctr/Errors.hpp"
#include "HashUtils.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Bidirectional interning table mapping C++ types to dense `TypeId` indices.
 *
 * `TypeId` is the compact `uint32_t` used throughout the runtime index and all hot
 * paths. The full qualified name and `std::type_index` are used to assign TypeIds
 * and to let `typeIdFor<T>()` resolve the compact id for a C++ runtime type.
 *
 * ### Startup map plus overflow
 *
 * During `start()`, `internByName()` assigns TypeIds in the startup maps. `freeze()`
 * is called at the end of `start()` and makes those maps read-only. Later runtime
 * bindings of previously unknown types continue the dense TypeId sequence through
 * an overflow table protected by `overflowLock_`.
 *
 * ### Runtime lookup
 *
 * `lookupByTypeIndex()` first reads the startup reverse map without locking. On a
 * hit, this is the unchanged hot path for startup-known types. Only the miss branch
 * takes a shared lock and checks the overflow table.
 *
 * ### Thread safety
 *
 * Startup interning and `freeze()` run during `start()` under its exclusive lock.
 * Overflow interning is synchronized by `overflowLock_`, which is independent from
 * the registry write lock. `lookupByTypeIndex()` is lock-free on startup-map hits
 * and takes a shared overflow lock only on misses.
 */
class TypeInterning {
public:
    /**
     * @brief Interns a type by qualified name and registers its `std::type_index`.
     *
     * - Startup new name: assigns the next startup `TypeId` and stores the
     *   `std::type_index` obtained from `typeInfoGetter()` in the startup reverse map.
     * - Post-freeze new name: assigns the next dense overflow `TypeId`.
     * - Known name: returns the existing `TypeId`. Duplicate discovery contributions
     *   for the same type are expected and handled correctly here.
     *
     * @pre `qualifiedName` must be non-null and non-empty.
     *
     * @param qualifiedName Stable compile-time string.
     * @param typeInfoGetter Function pointer returning `typeid(T)`.
     * @return Dense `TypeId` assigned to this qualified name.
     */
    TypeId internByName(const char* qualifiedName,
                        const std::type_info& (*typeInfoGetter)()) {
        assert(qualifiedName && qualifiedName[0] != '\0');

        // C1: key is string_view (no heap allocation, even on miss).
        // qualifiedName comes from qualifiedNameOf() via define_static_string:
        // static storage lifetime guaranteed.
        const std::string_view sv{qualifiedName};
        auto it = nameToId_.find(sv);
        if (it != nameToId_.end()) return it->second;

        if (!frozen_) {
            auto [ins, _] = nameToId_.emplace(sv, TypeId{0}); // no string copy
            const TypeId id = static_cast<TypeId>(idToName_.size());
            ins->second = id;
            idToName_.push_back(ins->first.data()); // stable: points into static string
            indexToId_.emplace(std::type_index(typeInfoGetter()), id);
            return id;
        }

        {
            std::shared_lock lock(overflowLock_);
            const auto overflowIt = overflowNameToId_.find(sv);
            if (overflowIt != overflowNameToId_.end())
                return overflowIt->second;
        }

        std::unique_lock lock(overflowLock_);
        const auto overflowIt = overflowNameToId_.find(sv);
        if (overflowIt != overflowNameToId_.end())
            return overflowIt->second;

        const TypeId id = static_cast<TypeId>(idToName_.size() + overflowIdToName_.size());
        auto [ins, _] = overflowNameToId_.emplace(sv, id);
        overflowIdToName_.push_back(ins->first.data());
        overflowIndexToId_.emplace(std::type_index(typeInfoGetter()), id);
        return id;
    }

    /**
     * @brief Switches post-start interning to the overflow table.
     *
     * After `freeze()`, startup maps are read-only. New names are assigned dense
     * TypeIds in the overflow table; `lookupByTypeIndex()` remains lock-free for
     * startup-map hits.
     */
    void freeze() noexcept { frozen_ = true; }

    /** @brief True after `freeze()`. */
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    /**
     * @brief Looks up the `TypeId` for a runtime type by its `std::type_index`.
     *
     * Called by `typeIdFor<T>()` in the Registry on its first invocation for T.
     * Returns `kInvalidTypeId` when the type is unknown. The startup-map hit path
     * is lock-free; the overflow table is checked only after a startup-map miss.
     *
     * @param index `std::type_index` wrapping `typeid(T)`.
     * @return Dense `TypeId`, or `kInvalidTypeId` if the type was never interned.
     */
    [[nodiscard]] TypeId lookupByTypeIndex(std::type_index index) const noexcept {
        const auto it = indexToId_.find(index);
        if (it != indexToId_.end()) return it->second;

        std::shared_lock lock(overflowLock_);
        const auto overflowIt = overflowIndexToId_.find(index);
        return overflowIt != overflowIndexToId_.end() ? overflowIt->second : kInvalidTypeId;
    }

    /**
     * @brief Returns the qualified name for a `TypeId`, for use in diagnostics.
     * @pre `id < size()`.
     */
    [[nodiscard]] const char* nameOf(TypeId id) const noexcept {
        const auto idx = static_cast<std::size_t>(id);
        const auto baseSize = idToName_.size();
        if (idx < baseSize)
            return idToName_[idx];

        std::shared_lock lock(overflowLock_);
        const auto overflowIdx = idx - baseSize;
        assert(overflowIdx < overflowIdToName_.size());
        return overflowIdToName_[overflowIdx];
    }

    /** @brief Number of interned types. */
    [[nodiscard]] std::size_t size() const noexcept {
        std::shared_lock lock(overflowLock_);
        return idToName_.size() + overflowIdToName_.size();
    }

    /**
     * @brief Pre-allocates buckets in all startup maps.
     *
     * Call before the start() intern loop to avoid repeated rehashing.
     * @param n Upper bound on the number of distinct startup types to be interned.
     */
    void reserve(std::size_t n) {
        nameToId_.reserve(n);
        indexToId_.reserve(n);
        idToName_.reserve(n);
    }

private:
    /// C1: key is string_view; the underlying data comes from define_static_string
    /// (program-lifetime const char*) - no std::string allocation on intern or lookup.
    std::unordered_map<std::string_view, TypeId> nameToId_;
    /// Reverse map: const char* pointers to the same static strings used as keys.
    /// Stable because the strings are program-lifetime; does not depend on map internals.
    std::vector<const char*> idToName_;
    std::unordered_map<std::type_index, TypeId> indexToId_; ///< Runtime reverse map for typeIdFor<T>.

    mutable std::shared_mutex overflowLock_;
    std::unordered_map<std::string_view, TypeId> overflowNameToId_;
    std::vector<const char*> overflowIdToName_;
    std::unordered_map<std::type_index, TypeId> overflowIndexToId_;

    bool frozen_ = false;
};

} // namespace CTORIUM_NAMESPACE::detail
