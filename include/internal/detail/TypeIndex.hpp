#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

#include "../DescriptorId.hpp"
#include "../NameId.hpp"
#include "../NameTable.hpp"
#include "../TypeId.hpp"

namespace ctr::detail {

/**
 * @brief Two-level index mapping `(TypeId, NameId)` pairs to sorted candidate lists.
 *
 * This is the data structure queried by `resolve<T>()` on the hot path.
 *
 * ### Structure  (specs-internal §8.2 — "dense flat", option B)
 * ```
 * vector<NameTable*>   typeIndex_         — TypeId → heap-allocated NameTable
 *                                           (pointer stable across outer-vector growth)
 * NameTable {
 *   flat_map<NameId, vector<DescriptorId>>  entries
 * }
 * ```
 * `NameTable` objects live on the heap and are owned by `ownedTables_`.  The outer
 * `typeIndex_` vector holds raw pointers into that ownership list.  Growing
 * `typeIndex_` (new TypeId) may reallocate its storage, but existing `NameTable`
 * heap addresses remain stable.
 *
 * ### Sort invariant
 * After `sortAllCandidates()`, every candidate vector is sorted in **descending
 * priority order**.  `resolve<T>()` reads the head element in O(1) and checks
 * index 1 for ambiguity (one extra read).  No further sorting is needed at runtime.
 *
 * ### Thread safety
 * `insertCandidate()` and `sortAllCandidates()` run during `start()` under its lock,
 * or during post-`start()` `bind*()` calls under the registry write lock.
 * `candidatesFor()` is lock-free after `start()`.
 */
class TypeIndex {
public:
    /**
     * @brief Registers a candidate descriptor for the given `(TypeId, NameId)` pair.
     *
     * Before `sortAllCandidates()` is called: pushes to a pending buffer (no flat_map
     * touch) so that the final construction is done in one sorted batch, avoiding
     * O(N²) flat_map insertion behaviour for types with many named qualifiers.
     *
     * After `sortAllCandidates()`: inserts directly into the flat_map (used by
     * `registerContextBean` for the single BeanContext descriptor added post-sort).
     *
     * @param typeId Interned TypeId of the exposed type.
     * @param nameId Interned NameId of the named qualifier (`kUnnamed` for unnamed beans).
     * @param descId DescriptorId of the candidate to register.
     */
    void insertCandidate(TypeId typeId, NameId nameId, DescriptorId descId) {
        if (!finalized_) {
            // Build phase: accumulate into pending buffer.
            ensurePending(typeId);
            pending_[static_cast<std::size_t>(typeId)].push_back({nameId, descId});
        } else {
            // Post-sort phase (e.g. registerContextBean): direct flat_map insert.
            ensureTable(typeId);
            table(typeId).entries.try_emplace(nameId).first->second.push_back(descId);
        }
    }

    /**
     * @brief Flushes pending candidates into sorted flat_map entries, then sorts by priority.
     *
     * For each type's pending (NameId, DescriptorId) pairs:
     *  1. Sort by NameId so that flat_map receives elements in ascending key order
     *     (O(M log M) once instead of O(M²) for M try_emplace calls).
     *  2. Group and populate the flat_map entries in O(M) via sorted-range insertion.
     *  3. Sort each candidate vector by descending priority.
     *
     * After this call, `insertCandidate` switches to direct flat_map insertion.
     *
     * @param priorityOf Callable `(DescriptorId) → int32_t` returning the descriptor's
     *                   priority.  Typically a lambda reading `descriptorTable.at(id).priority`.
     */
    template <typename PriorityFn>
    void sortAllCandidates(PriorityFn&& priorityOf) {
        for (std::size_t i = 0; i < pending_.size(); ++i) {
            auto& pend = pending_[i];
            if (pend.empty()) continue;

            ensureTable(static_cast<TypeId>(i));
            auto& tbl = table(static_cast<TypeId>(i));

            // Sort by NameId ascending so flat_map receives keys in order.
            std::sort(pend.begin(), pend.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

            // Group by NameId and insert — flat_map insertion is O(1) amortized
            // when keys arrive in sorted order (appended to the sorted key array).
            for (auto& [nameId, descId] : pend) {
                tbl.entries.try_emplace(nameId).first->second.push_back(descId);
            }
            pend.clear();
            pend.shrink_to_fit();
        }
        pending_.clear();
        pending_.shrink_to_fit();

        // Sort each candidate vector by descending priority; compute derived state.
        const std::size_t typeCount = typeIndex_.size();
        ambiguous_.clear();
        ambiguous_.resize(typeCount);   // default-constructs null unique_ptrs
        singleUnnamed_.assign(typeCount, kInvalidDescriptorId);

        for (std::size_t i = 0; i < typeCount; ++i) {
            auto* t = typeIndex_[i];
            if (!t) continue;
            // flat_map iterates keys in ascending NameId order.
            for (auto&& [nameId, vec] : t->entries) {
                std::sort(vec.begin(), vec.end(),
                    [&](DescriptorId a, DescriptorId b) {
                        return priorityOf(a) > priorityOf(b);
                    });
                // Precompute ambiguity: two top candidates share the highest priority.
                const bool ambig = vec.size() >= 2
                    && priorityOf(vec[0]) == priorityOf(vec[1]);
                if (ambig) {
                    if (!ambiguous_[i])
                        ambiguous_[i] = std::make_unique<std::vector<NameId>>();
                    // Keys arrive in ascending order (flat_map iteration) → already sorted.
                    ambiguous_[i]->push_back(nameId);
                }
                // Precompute mono-candidate unnamed fast-path.
                if (nameId == kUnnamed && vec.size() == 1 && !ambig)
                    singleUnnamed_[i] = vec[0];
            }
        }
        finalized_ = true;
    }

    /**
     * @brief Returns the sorted candidate vector for `(typeId, nameId)`, or `nullptr`.
     *
     * The returned pointer is valid for the lifetime of the `TypeIndex`.
     *
     * Callers interpret the result as follows:
     * - `nullptr` or empty → no candidate → `ResolutionError`.
     * - Non-empty → head is highest-priority candidate.
     * - `size() >= 2 && priority[0] == priority[1]` → ambiguity → `ResolutionError`.
     *
     * Lock-free after `start()`.
     *
     * @param typeId TypeId of the requested type.
     * @param nameId NameId of the qualifier; `kUnnamed` for the unnamed resolution space.
     * @return Pointer to the sorted candidate vector, or `nullptr` if absent.
     */
    [[nodiscard]] const std::vector<DescriptorId>*
    candidatesFor(TypeId typeId, NameId nameId) const noexcept {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= typeIndex_.size() || !typeIndex_[idx]) return nullptr;
        const auto it = typeIndex_[idx]->entries.find(nameId);
        return it != typeIndex_[idx]->entries.end() ? &it->second : nullptr;
    }

    /**
     * @brief Returns the full `NameTable` for a `TypeId`, or `nullptr` if unregistered.
     *
     * Used by `Registry::start()` Phase 3.5 to iterate all `(NameId, candidates)` pairs
     * for a given type and validate the graph.
     *
     * @param typeId TypeId to look up.
     * @return Pointer to the NameTable, or nullptr if no bean of this type was contributed.
     */
    [[nodiscard]] const NameTable* tableFor(TypeId typeId) const noexcept {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= typeIndex_.size() || !typeIndex_[idx]) return nullptr;
        return typeIndex_[idx];
    }

    /**
     * @brief Returns true when (typeId, nameId) has two or more candidates with equal
     *        top priority.  Precomputed once in `sortAllCandidates`; O(log N) binary
     *        search where N = number of ambiguous keys for the type (typically 0).
     *
     * Lock-free after `start()`.
     */
    [[nodiscard]] bool isAmbiguousFor(TypeId typeId, NameId nameId) const noexcept {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= ambiguous_.size() || !ambiguous_[idx]) return false;
        const auto& v = *ambiguous_[idx];
        return std::binary_search(v.begin(), v.end(), nameId);
    }

    /**
     * @brief Returns the single unnamed candidate for typeId, or `kInvalidDescriptorId`.
     *
     * Valid only when there is exactly one candidate for `(typeId, kUnnamed)` and it is
     * unambiguous.  Precomputed in `sortAllCandidates`.  Enables the fast-path in
     * `resolve<T>()` that skips the NameTable → flat_map → vector chain entirely.
     *
     * Lock-free after `start()`.
     */
    [[nodiscard]] DescriptorId singleUnnamedCandidate(TypeId typeId) const noexcept {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= singleUnnamed_.size()) return kInvalidDescriptorId;
        return singleUnnamed_[idx];
    }

private:
    void ensureTable(TypeId typeId) {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= typeIndex_.size()) typeIndex_.resize(idx + 1, nullptr);
        if (!typeIndex_[idx]) {
            ownedTables_.push_back(std::make_unique<NameTable>());
            typeIndex_[idx] = ownedTables_.back().get();
        }
    }

    void ensurePending(TypeId typeId) {
        const auto idx = static_cast<std::size_t>(typeId);
        if (idx >= pending_.size()) pending_.resize(idx + 1);
    }

    [[nodiscard]] NameTable& table(TypeId typeId) noexcept {
        return *typeIndex_[static_cast<std::size_t>(typeId)];
    }

    /// Pointer table: TypeId → NameTable*. Null slots for unregistered TypeIds.
    std::vector<NameTable*> typeIndex_;
    /// Owns all NameTable allocations. Pointers in typeIndex_ alias into this.
    std::vector<std::unique_ptr<NameTable>> ownedTables_;
    /// Pending (NameId, DescriptorId) pairs per TypeId, used during start() build phase.
    /// Flushed and cleared by sortAllCandidates().
    std::vector<std::vector<std::pair<NameId, DescriptorId>>> pending_;
    /// True after sortAllCandidates(); switches insertCandidate to direct flat_map insert.
    bool finalized_ = false;
    /// Per-TypeId sorted list of ambiguous NameIds (top two candidates share priority).
    /// Null entry = no ambiguity for that type.  Computed in sortAllCandidates.
    /// Binary-searched by isAmbiguousFor() in O(log N) where N is typically 0.
    std::vector<std::unique_ptr<std::vector<NameId>>> ambiguous_;
    /// Per-TypeId: DescriptorId of the single unnamed candidate, or kInvalidDescriptorId.
    /// Enables fast-path in resolve<T>() for the common mono-candidate unnamed case.
    std::vector<DescriptorId> singleUnnamed_;
};

} // namespace ctr::detail
