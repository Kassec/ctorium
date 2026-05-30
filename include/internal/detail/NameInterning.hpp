#pragma once

#include <cassert>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../NameId.hpp"
#include "HashUtils.hpp"

namespace ctr::detail {

/**
 * @brief Bidirectional interning table mapping bean named-qualifier strings to `NameId`.
 *
 * `kUnnamed = 0` is pre-reserved for beans without `[[=ctr::named{...}]]`.
 * Named keys are assigned starting from `NameId = 1`.
 *
 * ### Lifecycle
 * Populated during `start()` from the `beanName` fields of `ContributedDescriptor`
 * entries (empty string → `kUnnamed`; non-empty → new or existing `NameId ≥ 1`).
 * May also be extended by post-`start()` `bind*()` calls under the registry write
 * lock (NameId space does not freeze after `start()`, unlike TypeId space).
 *
 * ### Thread safety
 * `intern()` must be called under the registry write lock (held by `start()` or by
 * post-`start()` binding).  `lookup()` is called on any thread after `start()` and
 * is lock-free (the map is read-only between write-lock acquisitions).
 */
class NameInterning {
public:
    NameInterning() {
        // Slot 0 = kUnnamed: pre-assign without a map entry.
        // The empty-string fast path in intern() ensures "" always maps to kUnnamed.
        idToName_.push_back(""); // index 0 = kUnnamed
        nameToId_.reserve(8);
    }

    /**
     * @brief Interns a bean name and returns its stable `NameId`.
     *
     * Thread-safe: acquires `mutex_` exclusively (write path).
     * Lock acquisition order: always `mutex_` alone (never while holding an outer
     * lock that is itself taken while holding `mutex_`).
     *
     * - Empty string `""` → `kUnnamed = 0` (fast path, no map lookup).
     * - Non-empty string: returns an existing `NameId ≥ 1`, or assigns the next
     *   available id.  The string is copied into internal storage on first insertion.
     *
     * @param beanName Bean named qualifier.  Empty string means the unnamed key.
     * @return `kUnnamed` for empty input; dense `NameId ≥ 1` for named input.
     */
    [[nodiscard]] NameId intern(std::string_view beanName) {
        if (beanName.empty()) return kUnnamed;

        std::unique_lock<std::shared_mutex> lock(mutex_);
        // Transparent find: no heap alloc on hit.
        auto it = nameToId_.find(beanName);
        if (it != nameToId_.end()) return it->second;

        auto [ins, _] = nameToId_.emplace(std::string(beanName), NameId{0});
        const NameId id = static_cast<NameId>(idToName_.size());
        ins->second = id;
        idToName_.push_back(ins->first.c_str());
        return id;
    }

    /**
     * @brief Looks up the `NameId` for a name without inserting.
     *
     * Thread-safe: acquires `mutex_` shared (read path, concurrent with other lookups).
     *
     * @return The interned `NameId`, or `kInvalidNameId` if the name is unknown.
     */
    [[nodiscard]] NameId lookup(std::string_view beanName) const noexcept {
        if (beanName.empty()) return kUnnamed;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto it = nameToId_.find(beanName); // transparent: no std::string allocation
        return it != nameToId_.end() ? it->second : kInvalidNameId;
    }

    /**
     * @brief Returns the original name string for a `NameId`, for use in diagnostics.
     *
     * Thread-safe: acquires `mutex_` shared.
     * @pre `id < size()`.
     */
    [[nodiscard]] std::string_view nameOf(NameId id) const noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        assert(static_cast<std::size_t>(id) < idToName_.size());
        return idToName_[static_cast<std::size_t>(id)];
    }

    /**
     * @brief Total number of interned entries, including slot 0 (`kUnnamed`).
     */
    [[nodiscard]] std::size_t size() const noexcept { return idToName_.size(); }

private:
    /// A6: protects all reads and writes to nameToId_ and idToName_.
    /// Lock order (when nested with Registry::writeLock_):
    ///   writeLock_ (outer) → mutex_ (inner)  — only during start() intern calls.
    ///   mutex_ alone — for post-start internNameSafe() and lookup() calls.
    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, NameId,
                       StringViewHash, std::equal_to<>> nameToId_; ///< Forward map; owns string storage.
    std::vector<const char*>                idToName_; ///< Reverse map; pointers into nameToId_ keys.
};

} // namespace ctr::detail
