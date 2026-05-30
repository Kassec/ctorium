#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "../TypeId.hpp"
#include "../../api/ctr/Errors.hpp"
#include "HashUtils.hpp"

namespace ctr::detail {

/**
 * @brief Bidirectional interning table mapping C++ types to dense `TypeId` indices.
 *
 * `TypeId` is the compact `uint32_t` used throughout the runtime index and all hot
 * paths.  The full qualified name and `std::type_index` are only used during `start()`
 * to assign TypeIds; after `start()` all comparisons operate on `uint32_t`.
 *
 * ### Two-phase design
 *
 * **Phase 1 — `start()` interning** (`internByName`):
 *   Each `ContributedDescriptor` carries a qualified type name (compile-time string
 *   via reflection) and a `TypeInfoGetter<T>::get` function pointer.  During `start()`,
 *   `internByName()` assigns a fresh `TypeId` for each new name and registers the
 *   corresponding `std::type_index` for phase-2 lookups.
 *
 * **Phase 2 — runtime lookup** (`lookupByTypeIndex`):
 *   At resolution time, `typeIdFor<T>()` calls `lookupByTypeIndex(typeid(T))` once,
 *   caches the result in a per-type `static std::atomic<TypeId>`, and pays only a
 *   single relaxed atomic load on every subsequent call (specs-internal §10.5).
 *
 * ### Freeze semantics
 * `freeze()` must be called at the end of `start()`.  After that, `internByName()`
 * raises `ctr::ConfigurationError` — this enforces specs-api §8.1: "TypeId is frozen
 * after `start()`."  `lookupByTypeIndex()` remains valid and lock-free.
 *
 * ### Thread safety
 * `internByName()` and `freeze()` run during `start()` under its exclusive lock.
 * `lookupByTypeIndex()` is called on any thread after `start()` and is lock-free
 * (the map is read-only at that point).
 */
class TypeInterning {
public:
    /**
     * @brief Interns a type by qualified name and registers its `std::type_index`.
     *
     * - New name: assigns the next `TypeId` (starting at 0) and stores the
     *   `std::type_index` obtained from `typeInfoGetter()` in the reverse map.
     * - Known name: returns the existing `TypeId`.  Duplicate discovery contributions
     *   for the same type are expected and handled correctly here (no error).
     *
     * @pre Table must not be frozen.
     * @pre `qualifiedName` must be non-null and non-empty.
     *
     * @param qualifiedName  Stable compile-time string (e.g. from
     *                       `std::meta::qualified_name_of`).
     * @param typeInfoGetter Function pointer returning `typeid(T)`.  Used to build
     *                       the `std::type_index → TypeId` reverse map consumed by
     *                       `lookupByTypeIndex()` at runtime.
     * @return Dense `TypeId` in `[0, N)` assigned to this qualified name.
     * @throws ctr::ConfigurationError if called after `freeze()`.
     */
    TypeId internByName(const char* qualifiedName,
                        const std::type_info& (*typeInfoGetter)()) {
        if (frozen_) {
            throw ctr::ConfigurationError(
                std::string("TypeInterning: cannot intern '") + qualifiedName
                + "' after start() has completed (TypeId space is frozen).");
        }
        assert(qualifiedName && qualifiedName[0] != '\0');

        // C1: key is string_view (no heap allocation, even on miss).
        // qualifiedName comes from qualifiedNameOf() via define_static_string —
        // static storage lifetime guaranteed.
        const std::string_view sv{qualifiedName};
        auto it = nameToId_.find(sv);
        if (it != nameToId_.end()) return it->second;

        auto [ins, _] = nameToId_.emplace(sv, TypeId{0}); // no string copy
        const TypeId id = static_cast<TypeId>(idToName_.size());
        ins->second = id;
        idToName_.push_back(ins->first.data()); // stable: points into static string
        indexToId_.emplace(std::type_index(typeInfoGetter()), id);
        return id;
    }

    /**
     * @brief Seals the table against further interning.  Must be called at end of `start()`.
     *
     * After `freeze()`, `internByName()` raises `ConfigurationError`.
     * `lookupByTypeIndex()` remains valid and lock-free.
     */
    void freeze() noexcept { frozen_ = true; }

    /** @brief True after `freeze()`. */
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    /**
     * @brief Looks up the `TypeId` for a runtime type by its `std::type_index`.
     *
     * Called by `typeIdFor<T>()` in the Registry on its first invocation for T.
     * Returns `kInvalidTypeId` when the type is unknown (never contributed via
     * `discover<>()` or `bind*()`); the Registry converts this to `ResolutionError`.
     *
     * Safe for concurrent read access after `freeze()`.
     *
     * @param index `std::type_index` wrapping `typeid(T)`.
     * @return Dense `TypeId`, or `kInvalidTypeId` if the type was never interned.
     */
    [[nodiscard]] TypeId lookupByTypeIndex(std::type_index index) const noexcept {
        const auto it = indexToId_.find(index);
        return it != indexToId_.end() ? it->second : kInvalidTypeId;
    }

    /**
     * @brief Returns the qualified name for a `TypeId`, for use in diagnostics.
     * @pre `id < size()`.
     */
    [[nodiscard]] const char* nameOf(TypeId id) const noexcept {
        assert(static_cast<std::size_t>(id) < idToName_.size());
        return idToName_[static_cast<std::size_t>(id)];
    }

    /** @brief Number of interned types. */
    [[nodiscard]] std::size_t size() const noexcept { return idToName_.size(); }

    /**
     * @brief Pre-allocates buckets in all three internal maps.
     *
     * Call before the start() intern loop to avoid repeated rehashing.
     * @param n Upper bound on the number of distinct types to be interned.
     */
    void reserve(std::size_t n) {
        nameToId_.reserve(n);
        indexToId_.reserve(n);
        idToName_.reserve(n);
    }

private:
    /// C1: key is string_view; the underlying data comes from define_static_string
    /// (program-lifetime const char*) — no std::string allocation on intern or lookup.
    std::unordered_map<std::string_view, TypeId> nameToId_;
    /// Reverse map: const char* pointers to the same static strings used as keys.
    /// Stable because the strings are program-lifetime; does not depend on map internals.
    std::vector<const char*> idToName_;
    std::unordered_map<std::type_index, TypeId>  indexToId_;  ///< Runtime reverse map for typeIdFor<T>.
    bool frozen_ = false;
};

} // namespace ctr::detail
