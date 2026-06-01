#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // TLCleanup::~TLCleanup
    //
    // Calls Registry::cleanupCurrentThread() for each registry this thread touched.
    // Runs on the exiting thread — the full destruction lifecycle fires on that thread.
    // ─────────────────────────────────────────────────────────────────────────────

    inline TLCleanup::~TLCleanup() noexcept {
        auto &tl = tlData();
        for (const auto &entry : registered) {
            const std::uint32_t id = entry.first;
            auto reg = entry.second.lock();
            // lock() fails if the Registry was destroyed without stop(); skip to avoid UAF.
            if (reg && tl.hasEntriesFor(id)) {
                reg->cleanupCurrentThread();
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeThreadLocalInstance
    //
    // Finds or creates the threadLocal instance for (this, primary descId) on the calling
    // thread.  No lock needed — the TL store is thread-private.  Registration with
    // the registry (for stop()-time cleanup) is done once per (thread, registry)
    // pair under tlMutex_.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void *Registry::materializeThreadLocalInstance(
        DescriptorId descId,
        ResolutionContext &ctx
        ) {
        const Descriptor &requestedDesc = descriptors_.at(descId);
        const DescriptorId primaryDescId =
            requestedDesc.primaryDescriptor != descId
            && requestedDesc.primaryDescriptor != kInvalidDescriptorId
                ? requestedDesc.primaryDescriptor
                : descId;
        const Descriptor &desc =
            primaryDescId == descId ? requestedDesc : descriptors_.at(primaryDescId);
        const DescriptorCold &cold = descriptors_.coldAt(primaryDescId);
        TLData &tl = tlData();

        // Fast path: already materialized on this thread.
        void *instance = tl.findInstance(registryId(), primaryDescId);
        if (instance != nullptr)
            return instance;

        // First-touch registration: register this thread's TLData with the registry.
        TLCleanup &cleanup = tlCleanup();
        const std::uint32_t id = registryId();
        if (cleanup.registered.find(id) == cleanup.registered.end()) {
            cleanup.registered.emplace(id, weak_from_this());
            std::lock_guard tlLock(tlMutex_);
            tlThreadStores_.push_back(&tl);
        }

        // Materialize on this thread (thread-private; no DCLP needed).
        CycleGuard guard(materializationStack(), primaryDescId);

        void *mem = nullptr;
        try {
            if (cold.allocAndConstruct != nullptr) {
                mem = cold.allocAndConstruct(static_cast<void *>(&ctx));
            } else {
                mem = ::operator new(cold.size, std::align_val_t{cold.align});
                cold.construct(mem, static_cast<void *>(&ctx));
            }
        } catch (...) {
            if (mem) {
                if (cold.dealloc != nullptr) {
                    cold.dealloc(mem);
                } else {
                    ::operator delete(mem, cold.size, std::align_val_t{cold.align});
                }
            }
            throw;
        }

        tl.storeInstance(registryId(), primaryDescId, mem);

        // Lifecycle: C++ construction → onInitialized → postConstruct → onCreated.
        ctr::AnyBean anyBean;
        anyBean.object_ = mem;
        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
        anyBean.bits_.f1.descId = primaryDescId;
        anyBean.registry_ = this;
        listeners_.dispatch(ListenerStore::phaseInitialized(), cold.exposedType, &anyBean);
        if (cold.postConstruct)
            cold.postConstruct(mem, static_cast<void *>(&ctx));
        listeners_.dispatch(ListenerStore::phaseCreated(), cold.exposedType, &anyBean);

        return mem;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::cleanupCurrentThread
    //
    // Called by TLCleanup::~TLCleanup on the exiting thread.
    // Collect+erase under tlMutex_ (prevents double-destroy with stop()); destroy
    // outside the lock so user preDestroy hooks don't hold tlMutex_.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::cleanupCurrentThread() noexcept {
        TLData &tl = tlData();
        std::vector<std::pair<DescriptorId, void *>> toDestroy;
        {
            std::lock_guard tlLock(tlMutex_);
            tl.collectFor(registryId(), toDestroy);
            // Remove this thread's TLData from the registry's list.
            auto &stores = tlThreadStores_;
            stores.erase(std::remove(stores.begin(), stores.end(), &tl), stores.end());
        }
        // Destroy in reverse insertion order (on the exiting thread — lifecycle compliant).
        for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
            executeDestructionLifecycle(it->first, it->second);
        }
    }

} // namespace ctr::detail
