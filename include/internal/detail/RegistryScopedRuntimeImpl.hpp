#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeOne<T>
    //
    // Thin typed wrapper around the shared non-template materialization body.
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    auto Registry::materializeOne(
        DescriptorId descId, ResolutionContext &ctx
        )
        -> ctr::Bean<T> {
        const MaterializedBeanHandle handle = materializeOneImpl(descId, ctx);
        switch (handle.form()) {
        case MaterializedHandleForm::Direct:
            return ctr::Bean<T>::makeDirect(
                static_cast<T *>(handle.instance()),
                handle.slot(),
                handle.descId(),
                this
                );
        case MaterializedHandleForm::Proxy:
            return ctr::Bean<T>::makeProxy(
                handle.scopeNameId(),
                handle.descId(),
                this);
        case MaterializedHandleForm::ThreadLocal:
            return ctr::Bean<T>::makeThreadLocal(handle.descId(), this);
        }

        throw ctr::ConfigurationError("Registry::resolve: unhandled materialized handle form.");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeSessionInstance
    //
    // Two-phase lazy materialization of a session bean into scope's SessionStore.
    // Uses the same writeLock_ / cv_ as the singleton path; uses separate
    // materializing_ entries keyed by (primary descId, scope) so that the same
    // primary descriptor in different scopes materializes independently.
    // Dispatches onInitialized → postConstruct → onCreated on first materialization.
    // Returns the live pointer; throws ResolutionError on dependency cycle.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void *Registry::materializeSessionInstance(
        DescriptorId descId,
        ctr::ScopedContext *scope,
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
        const SessionSlot slot = desc.sessionSlot;

        // Fast path: already materialized (lock-free acquire).
        void *instance = scope->sessionStore_.find(slot);
        if (instance != nullptr)
            return instance;

        bool didMaterialize = false;

        // RuntimeBinding session beans must be pre-stored in sessionStore_ at
        // scope start(). A nil slot means the binding was not renewed for this cycle.
        if (desc.origin == Origin::RuntimeBinding) {
            throw ctr::ContextStateError(
                "Registry::materializeSessionInstance: bound session bean has no "
                "instance for this scope cycle; call bindSession<T>() before start()."
                );
        }

        // Phase 1: claim (primary descId, scope) or wait for a peer on the same scope.
        didMaterialize = claimMaterializationOrWait(
            primaryDescId,
            scope,
            [&] { return scope->sessionStore_.find(slot); },
            "Registry::resolve: dependency cycle detected during "
            "session bean materialization.",
            "Registry::resolve: cross-thread dependency cycle detected during "
            "session bean materialization."
            );
        if (!didMaterialize) {
            instance = scope->sessionStore_.find(slot);
        }

        // Phase 2: construct outside the lock.
        if (didMaterialize) {
            void *mem = nullptr;
            try {
                if (desc.allocAndConstruct != nullptr) {
                    mem = desc.allocAndConstruct(static_cast<void *>(&ctx));
                } else {
                    mem = ::operator new(desc.size, std::align_val_t{desc.align});
                    desc.construct(mem, static_cast<void *>(&ctx));
                }
            } catch (...) {
                if (mem) {
                    if (desc.dealloc != nullptr) {
                        desc.dealloc(mem);
                    } else {
                        ::operator delete(mem, desc.size, std::align_val_t{desc.align});
                    }
                }
                {
                    std::lock_guard reLock(writeLock_);
                    auto it = std::find_if(
                        materializing_.begin(),
                        materializing_.end(),
                        [primaryDescId, scope](const MaterializingEntry& p) {
                            return p.key.descId == primaryDescId && p.key.scope == scope;
                        }
                        );
                    if (it != materializing_.end()) {
                        *it = materializing_.back();
                        materializing_.pop_back();
                    }
                    materializationStack().pop_back();
                }
                cv_.notify_all();
                throw;
            }

            // Phase 3: store under lock, then wake waiters.
            {
                std::lock_guard reLock(writeLock_);
                scope->sessionStore_.store(slot, primaryDescId, mem);
                auto it = std::find_if(
                    materializing_.begin(),
                    materializing_.end(),
                    [primaryDescId, scope](const MaterializingEntry& p) {
                        return p.key.descId == primaryDescId && p.key.scope == scope;
                    }
                    );
                if (it != materializing_.end()) {
                    *it = materializing_.back();
                    materializing_.pop_back();
                }
                materializationStack().pop_back();
            }
            cv_.notify_all();
            instance = mem;

            // Lifecycle dispatch outside lock: onInitialized → postConstruct → onCreated.
            ctr::AnyBean anyBean;
            anyBean.object_ = instance;
            anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
            anyBean.bits_.f1.descId = primaryDescId;
            anyBean.registry_ = this;

            listeners_.dispatch(ListenerStore::phaseInitialized(), desc.exposedType, &anyBean);
            if (desc.postConstruct) {
                desc.postConstruct(instance, static_cast<void *>(&ctx));
            }
            listeners_.dispatch(ListenerStore::phaseCreated(), desc.exposedType, &anyBean);
        }

        return instance;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::stopRegistryOwnedBeans
    //
    // Marks the registry stopped, then destroys registry-owned singleton and
    // prototype instances in the same order used by Registry::stop(). Thread-local
    // instances are intentionally excluded; explicit stop() owns that sweep.
    // ─────────────────────────────────────────────────────────────────────────────

    inline bool Registry::stopRegistryOwnedBeans() noexcept {
        // A5: mark stopped under writeLock_, then release before destructions so
        // that listener callbacks fired during executeDestructionLifecycle can call
        // remove() or inspect the context without deadlocking on writeLock_.
        {
            std::lock_guard lock(writeLock_);
            if (!started_.load(std::memory_order_relaxed)) return false;
            // Mark stopped first so that Bean<T> destructors triggered by d.destroy()
            // see startedRelaxed()==false and skip releaseIfPrototype(), preventing
            // double-destroy when a bean holds a Bean<T> member to another prototype.
            started_.store(false, std::memory_order_relaxed);
        }

        // Destructions run without writeLock_; started_=false prevents new resolves.

        // Singletons: reverse insertion order.
        const auto &order = singletons_.insertionOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            void *mem = singletons_.find(*it);
            if (mem != nullptr)
                executeDestructionLifecycle(*it, mem);
        }
        singletons_.releaseAll();

        // Prototypes: reverse slot index, matching the previous stop() sweep.
        PrototypeStore& prototypes = prototypeStore();
        const std::size_t count = prototypes.slotCount();
        for (std::size_t s = count; s > 0; --s) {
            const auto [mem, descId] =
                prototypes.takeSlotMetaForDestruction(static_cast<SlotId>(s - 1));
            if (mem != nullptr) {
                executeDestructionLifecycle(descId, mem);
            }
        }

        return true;
    }

    inline void Registry::rollbackFailedStart() noexcept {
        startLifecyclePending_.clear();
        (void)stopRegistryOwnedBeans();
    }

    // Registry::stopScope
    //
    // Destroys all session instances owned by `scope` in reverse construction order,
    // then clears the session store.  Marks the scope stopped before sweeping so
    // that preDestroy hooks see an already-stopped scope.
    // Called from ScopedContext::stop().
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::stopScope(ctr::ScopedContext &scope) noexcept {
        // A5: mark scope stopped and capture insertion order under writeLock_, then
        // release the lock before calling executeDestructionLifecycle so that listener
        // callbacks triggered during destruction can call registry operations without
        // deadlocking on writeLock_.
        std::vector<DescriptorId> order;
        {
            std::lock_guard lock(writeLock_);
            scope.scopeStarted_ = false;
            order = scope.sessionStore_.insertionOrder(); // copy under lock
        }

        // Destructions outside writeLock_.
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const SessionSlot slot = descriptors_.at(*it).sessionSlot;
            void *mem = scope.sessionStore_.find(slot); // lock-free acquire
            if (mem != nullptr)
                executeDestructionLifecycle(*it, mem);
        }

        // Reacquire writeLock_ for the final store reset.
        std::lock_guard lock(writeLock_);
        scope.sessionStore_.releaseAll();
    }

} // namespace ctr::detail
