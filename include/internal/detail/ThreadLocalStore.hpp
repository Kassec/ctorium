#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../DescriptorId.hpp"

namespace ctr::detail {

// Forward declaration: TLCleanup holds a weak_ptr<Registry>.
class Registry;

/**
 * @brief Per-thread storage for thread-local bean instances.
 *
 * One TLData lives in thread-local storage per thread. It maps each registry
 * ID to entries indexed directly by DescriptorId for O(1) lookup on the hot
 * path (Form 3 `operator->`). A per-registry insertion-order vector enables
 * reverse-order destruction at thread exit or `root.stop()`.
 *
 * All access is thread-private (read/write from the owning thread only),
 * except `collectFor` which is also called under `Registry::tlMutex_` from
 * `stop()` running on any thread — user code guarantees no concurrent bean
 * access at that point (specs-api §18).
 */
struct TLData {
    using Key = std::pair<std::uint32_t, DescriptorId>;

    struct RegistryEntries {
        std::vector<void*> instances;
        std::vector<DescriptorId> order;
    };

    struct InstanceIndex {
        struct Iterator {
            Key key{};
            void *second = nullptr;
            bool present = false;

            [[nodiscard]] const Iterator* operator->() const noexcept {
                return this;
            }

            friend bool operator==(const Iterator &lhs, const Iterator &rhs) noexcept {
                return lhs.present == rhs.present
                    && (!lhs.present || lhs.key == rhs.key);
            }

            friend bool operator!=(const Iterator &lhs, const Iterator &rhs) noexcept {
                return !(lhs == rhs);
            }
        };

        std::unordered_map<std::uint32_t, RegistryEntries> registries;

        [[nodiscard]] void* findInstance(std::uint32_t registryId, DescriptorId descId) const noexcept {
            const auto regIt = registries.find(registryId);
            if (regIt == registries.end())
                return nullptr;
            const auto index = static_cast<std::size_t>(descId);
            const auto &values = regIt->second.instances;
            return index < values.size() ? values[index] : nullptr;
        }

        [[nodiscard]] bool storeInstance(std::uint32_t registryId, DescriptorId descId, void *ptr) {
            RegistryEntries &entry = registries[registryId];
            const auto index = static_cast<std::size_t>(descId);
            if (index >= entry.instances.size())
                entry.instances.resize(index + 1u, nullptr);
            const bool firstStore = entry.instances[index] == nullptr;
            if (firstStore)
                entry.order.push_back(descId);
            entry.instances[index] = ptr;
            return firstStore;
        }

        [[nodiscard]] Iterator find(const Key &key) const noexcept {
            void *ptr = findInstance(key.first, key.second);
            return ptr != nullptr ? Iterator{key, ptr, true} : end();
        }

        [[nodiscard]] Iterator end() const noexcept {
            return Iterator{};
        }

        void erase(Iterator it) noexcept {
            if (!it.present)
                return;
            const auto regIt = registries.find(it.key.first);
            if (regIt == registries.end())
                return;
            const auto index = static_cast<std::size_t>(it.key.second);
            auto &values = regIt->second.instances;
            if (index < values.size())
                values[index] = nullptr;
            auto &order = regIt->second.order;
            order.erase(
                std::remove(order.begin(), order.end(), it.key.second),
                order.end());
        }

        void eraseRegistry(std::uint32_t registryId) {
            registries.erase(registryId);
        }
    };

    InstanceIndex instances;
    std::vector<Key> order;  ///< Compatibility order for existing internal tests.

    /** O(1) lookup by DescriptorId; returns nullptr when not present. */
    [[nodiscard]] void* findInstance(std::uint32_t registryId, DescriptorId descId) const noexcept {
        return instances.findInstance(registryId, descId);
    }

    /** Record a newly materialized instance. */
    void storeInstance(std::uint32_t registryId, DescriptorId descId, void* ptr) {
        if (instances.storeInstance(registryId, descId, ptr))
            order.push_back({registryId, descId});
    }

    /** Returns true when at least one entry for `registryId` exists. */
    [[nodiscard]] bool hasEntriesFor(std::uint32_t registryId) const noexcept {
        const auto regIt = instances.registries.find(registryId);
        return regIt != instances.registries.end() && !regIt->second.order.empty();
    }

    /**
     * @brief Collects all instances for `registryId`, erases them from this store,
     * appends `(descId, ptr)` pairs to `out` in insertion order.
     * Called under `Registry::tlMutex_`.
     */
    void collectFor(std::uint32_t registryId,
                    std::vector<std::pair<DescriptorId, void*>>& out) {
        const auto regIt = instances.registries.find(registryId);
        if (regIt == instances.registries.end())
            return;
        const RegistryEntries &entry = regIt->second;
        for (DescriptorId descId : entry.order) {
            const auto index = static_cast<std::size_t>(descId);
            if (index < entry.instances.size()) {
                void *ptr = entry.instances[index];
                if (ptr != nullptr)
                    out.push_back({descId, ptr});
            }
        }
        instances.eraseRegistry(registryId);
        order.erase(
            std::remove_if(order.begin(), order.end(),
                           [registryId](const Key &key) { return key.first == registryId; }),
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
    std::unordered_map<std::uint32_t, std::weak_ptr<Registry>> registered;  ///< Registry ID → weak liveness handle.
    ~TLCleanup() noexcept;                     ///< Defined in BeanInlineImpl.hpp.
};

/** Returns the calling thread's TLCleanup (lazily initialised). */
inline TLCleanup& tlCleanup() {
    thread_local TLCleanup cleanup;
    return cleanup;
}

} // namespace ctr::detail
