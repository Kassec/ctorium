#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../DescriptorId.hpp"

namespace ctr::detail {

// Forward declaration: TLData uses Registry* as a map key.
class Registry;

/**
 * @brief Per-thread storage for thread-local bean instances.
 *
 * One TLData lives in thread-local storage per thread.  It maps
 * `(Registry*, DescriptorId)` to live instance pointers, keyed for O(1)
 * lookup on the hot path (Form 3 `operator->`).  An insertion-order vector
 * enables reverse-order destruction at thread exit or `root.stop()`.
 *
 * All access is thread-private (read/write from the owning thread only),
 * except `collectFor` which is also called under `Registry::tlMutex_` from
 * `stop()` running on any thread — user code guarantees no concurrent bean
 * access at that point (specs-api §18).
 */
struct TLData {
    using Key = std::pair<Registry*, DescriptorId>;

    struct PairHash {
        std::size_t operator()(const Key& k) const noexcept {
            auto h1 = std::hash<void*>{}(static_cast<void*>(k.first));
            auto h2 = std::hash<DescriptorId>{}(k.second);
            return h1 ^ (h2 * 2654435761UL);
        }
    };

    std::unordered_map<Key, void*, PairHash> instances;
    std::vector<Key>                         order;  ///< Insertion order for reverse destruction.

    /** O(1) lookup; returns nullptr when not present. */
    [[nodiscard]] void* findInstance(Registry* reg, DescriptorId descId) const noexcept {
        const auto it = instances.find({reg, descId});
        return it != instances.end() ? it->second : nullptr;
    }

    /** Record a newly materialized instance. */
    void storeInstance(Registry* reg, DescriptorId descId, void* ptr) {
        instances[{reg, descId}] = ptr;
        order.push_back({reg, descId});
    }

    /** Returns true when at least one entry for `reg` exists. */
    [[nodiscard]] bool hasEntriesFor(Registry* reg) const noexcept {
        for (const auto& key : order) {
            if (key.first == reg) return true;
        }
        return false;
    }

    /**
     * @brief Collects all instances for `reg`, erases them from this store,
     * appends `(descId, ptr)` pairs to `out` in insertion order.
     * Called under `Registry::tlMutex_`.
     */
    void collectFor(Registry* reg,
                    std::vector<std::pair<DescriptorId, void*>>& out) {
        for (const auto& key : order) {
            if (key.first == reg) {
                const auto it = instances.find(key);
                if (it != instances.end()) {
                    out.push_back({key.second, it->second});
                    instances.erase(it);
                }
            }
        }
        order.erase(
            std::remove_if(order.begin(), order.end(),
                           [reg](const Key& k) { return k.first == reg; }),
            order.end());
    }
};

/** Returns the calling thread's TLData (lazily initialised). */
inline TLData& tlData() {
    thread_local TLData data;
    return data;
}

/**
 * @brief Per-thread RAII sentinel.
 *
 * Exactly one per thread; created on first threadLocal resolution.
 * Its destructor fires when the thread exits and calls
 * `Registry::cleanupCurrentThread()` for every registry the thread touched,
 * executing the full bean destruction lifecycle on the exiting thread.
 */
struct TLCleanup {
    std::unordered_map<std::uint32_t, Registry*> registered;  ///< Registry ID to live cleanup target.
    ~TLCleanup() noexcept;                     ///< Defined in BeanInlineImpl.hpp.
};

/** Returns the calling thread's TLCleanup (lazily initialised). */
inline TLCleanup& tlCleanup() {
    thread_local TLCleanup cleanup;
    return cleanup;
}

} // namespace ctr::detail
