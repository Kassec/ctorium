#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::resolve<T>  — hot-path resolution (specs-internal §9)
    //
    // Lifetime coverage: Singleton, Prototype, Session, and ThreadLocal.
    // Hot handle exits stay here; construction paths are delegated to materializeOne<T>().
    //
    // Two-phase singleton slow path (ensures writeLock_ is NOT held during the
    // construct thunk, so injected singletons can recursively call resolve<U>()):
    //
    //   Phase 1 — under writeLock_:
    //     DCLP re-check, cycle detection, push materializationStack(),
    //     insert descId into materializing_, release lock.
    //
    //   Phase 2 — without lock:
    //     Allocate memory, call desc.construct (thunk may re-enter resolve).
    //
    //   Phase 3 — under writeLock_:
    //     Store constructed object, pop materializationStack(),
    //     erase descId from materializing_.
    //
    // Concurrent threads that encounter a descId in materializing_ wait on cv_;
    // while waiting, a per-thread edge records the key they are blocked on.
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    auto Registry::resolve(NameId nameId, ResolutionContext &ctx) -> Bean<T> {
        // 1. State check — acquire pairs with the release store in start(), ensuring all
        // structures populated during start() are visible to this resolving thread.
        if (!started_.load(std::memory_order_acquire)) [[unlikely]] {
            throw ctr::ContextStateError(
                "Registry::resolve: context has not been started; "
                "call start() before resolving beans."
                );
        }

        // 2. TypeId via per-type static atomic cache (single relaxed load on hot path).
        const TypeId typeId = typeIdFor<T>();
        if (typeId == kInvalidTypeId) [[unlikely]] {
            throw ctr::ResolutionError(
                "Registry::resolve: the requested type was not registered in "
                "this context via discover<>() or bindSingleton()."
                );
        }

        const bool hasAnyDefault = hasAnyDefault_.load(std::memory_order_relaxed);

        // Fast-path: mono-candidate unnamed, no effective default for this type.
        // Skips the NameTable → flat_map → vector chain and the ambiguity check.
        DescriptorId descId;
        NameId effectiveNameId = nameId;
        if (nameId == kUnnamed) {
            if (ctx.scope != nullptr
                    && ctx.scope->hasAnyScopedDefault_.load(std::memory_order_relaxed)) {
                effectiveNameId = ctx.scope->scopedDefaults_.getDefault(typeId);
            }
            if (effectiveNameId == kUnnamed && hasAnyDefault) {
                effectiveNameId = defaults_.getDefault(typeId);
            }
        }
        if (effectiveNameId == kUnnamed) [[likely]] {
            descId = typeIndex_.singleUnnamedCandidate(typeId);
            if (descId != kInvalidDescriptorId) [[likely]] {
                // Single unnamed candidate confirmed — fall through to materialization.
                goto materialize;
            }
        }

        {
            // 4 + 5. Single flat_map lookup: head DescriptorId + ambiguity flag.
            // Replaces candidatesFor() + isAmbiguousFor() (two lookups) with one.
            const auto [head, ambig] = typeIndex_.headFor(typeId, effectiveNameId);
            if (head == kInvalidDescriptorId) [[unlikely]] {
                // Post-start bindings update the canonical entries table; the
                // precomputed entry head only covers candidates known at start().
                const auto* candidates = typeIndex_.candidatesFor(typeId, effectiveNameId);
                if (!candidates || candidates->empty()) {
                    throw ctr::ResolutionError(
                        "Registry::resolve: no bean registered for the requested "
                        "type and named key."
                        );
                }
                descId = (*candidates)[0];
                if (candidates->size() >= 2
                    && descriptors_.at((*candidates)[0]).priority
                        == descriptors_.at((*candidates)[1]).priority) [[unlikely]] {
                    throw ctr::ResolutionError(
                        "Registry::resolve: ambiguous resolution — two candidates share "
                        "the highest priority for the requested type and named key."
                        );
                }
            } else {
                descId = head;
                if (ambig) [[unlikely]] {
                    throw ctr::ResolutionError(
                        "Registry::resolve: ambiguous resolution — two candidates share "
                        "the highest priority for the requested type and named key."
                        );
                }
            }
        }

    materialize:
        const Lifetime lifetime = descriptors_.lifetimeOf(descId);
        if (lifetime == Lifetime::Singleton) {
            void* instance = singletons_.find(descId);
            if (instance != nullptr) {
                return Bean<T>::makeDirect(
                    static_cast<T*>(instance),
                    kInvalidSlotId,
                    descId,
                    this);
            }
        } else if (lifetime == Lifetime::Session) {
            if (ctx.scope == nullptr) {
                throw ctr::ContextStateError(
                    "Registry::resolve: session beans can only be resolved from a "
                    "scoped context, not directly from a root context."
                    );
            }
            if (!ctx.scope->scopeStarted_) {
                throw ctr::ContextStateError(
                    "Registry::resolve: the scope is stopped; start the scope before resolving."
                    );
            }
            const Descriptor& desc = descriptors_.at(descId);
            const DescriptorId materializationDescId =
                desc.primaryDescriptor != descId
                && desc.primaryDescriptor != kInvalidDescriptorId
                    ? desc.primaryDescriptor
                    : descId;
            (void)materializeSessionInstance(materializationDescId, ctx.scope, ctx);
            return Bean<T>::makeProxy(ctx.scope->scopeNameId_, descId, this);
        } else if (lifetime == Lifetime::ThreadLocal) {
            const Descriptor& desc = descriptors_.at(descId);
            const DescriptorId materializationDescId =
                desc.primaryDescriptor != descId
                && desc.primaryDescriptor != kInvalidDescriptorId
                    ? desc.primaryDescriptor
                    : descId;
            (void)materializeThreadLocalInstance(materializationDescId, ctx);
            return Bean<T>::makeThreadLocal(descId, this);
        }
        return materializeOne<T>(descId, ctx);
    }

    inline std::uint32_t Registry::materializationThreadToken() noexcept {
        thread_local const std::uint32_t token =
            nextMaterializationThreadToken_.fetch_add(1, std::memory_order_relaxed) + 1;
        return token;
    }

    inline void Registry::ensureMaterializationWaitSlot(std::uint32_t threadToken) {
        std::unique_lock slotsLock(waitSlotsMutex_);
        if (threadToken < waitingByThread_.size())
            return;

        std::vector<MaterializationWait> grown(static_cast<std::size_t>(threadToken) + 1);
        {
            std::lock_guard lock(writeLock_);
            if (threadToken < waitingByThread_.size())
                return;
            std::copy(waitingByThread_.begin(), waitingByThread_.end(), grown.begin());
            waitingByThread_.swap(grown);
        }
    }

    inline bool Registry::materializationWaitCycleDetected(
        std::uint32_t currentThreadToken,
        std::uint32_t ownerThreadToken) const noexcept {
        std::uint32_t token = ownerThreadToken;
        const std::size_t limit = materializing_.size();
        for (std::size_t step = 0; step < limit; ++step) {
            if (token == currentThreadToken)
                return true;
            if (token == kNoMaterializationThreadToken
                    || token >= waitingByThread_.size()) {
                return false;
            }
            const MaterializationWait& wait = waitingByThread_[token];
            if (!wait.active)
                return false;

            const auto ownerIt = std::find_if(
                materializing_.begin(),
                materializing_.end(),
                [&wait](const MaterializingEntry& entry) {
                    return entry.key == wait.key;
                }
                );
            if (ownerIt == materializing_.end())
                return false;
            token = ownerIt->threadToken;
        }
        return false;
    }

    template <typename FindExisting>
    bool Registry::claimMaterializationOrWait(
        DescriptorId descId,
        ctr::ScopedContext* scope,
        FindExisting&& findExisting,
        const char* intraThreadCycleMessage,
        const char* crossThreadCycleMessage) {
        const std::uint32_t currentThreadToken = materializationThreadToken();
        ensureMaterializationWaitSlot(currentThreadToken);

        const MaterializationKey key{descId, scope};
        std::unique_lock lock(writeLock_);
        std::vector<DescriptorId>& stack = materializationStack();
        auto hasIntraThreadCycle = [&] {
            for (DescriptorId existing : stack) {
                if (existing == descId) {
                    return true;
                }
            }
            return false;
        };

        if (hasIntraThreadCycle()) {
            lock.unlock();
            throw ctr::ResolutionError(intraThreadCycleMessage);
        }
        while (true) {
            if (findExisting() != nullptr)
                return false;

            const auto ownerIt = std::find_if(
                materializing_.begin(),
                materializing_.end(),
                [&key](const MaterializingEntry& entry) {
                    return entry.key == key;
                }
                );
            if (ownerIt == materializing_.end()) {
                if (hasIntraThreadCycle()) {
                    lock.unlock();
                    throw ctr::ResolutionError(intraThreadCycleMessage);
                }
                stack.push_back(descId);
                materializing_.push_back({key, currentThreadToken});
                return true;
            }

            const std::uint32_t ownerThreadToken = ownerIt->threadToken;
            if (ownerThreadToken == currentThreadToken) {
                lock.unlock();
                throw ctr::ResolutionError(intraThreadCycleMessage);
            }
            if (!stack.empty()
                    && materializationWaitCycleDetected(
                        currentThreadToken,
                        ownerThreadToken)) {
                lock.unlock();
                throw ctr::ResolutionError(crossThreadCycleMessage);
            }

            waitingByThread_[currentThreadToken] = MaterializationWait{key, true};
            cv_.wait(lock);
            waitingByThread_[currentThreadToken].active = false;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeOneImpl
    //
    // Per-descriptor materialization: singleton (two-phase lock), prototype,
    // alias handling, session proxy preparation, thread-local preparation, and
    // lifecycle dispatch.  The returned type-erased data is consumed by the thin
    // materializeOne<T>() wrapper.
    // ─────────────────────────────────────────────────────────────────────────────

    inline MaterializedBeanHandle Registry::materializeOneImpl(
        DescriptorId descId, ResolutionContext &ctx
        ) {
        const Lifetime lt = descriptors_.lifetimeOf(descId);
        void *singletonInstance = nullptr;
        if (lt == Lifetime::Singleton) {
            singletonInstance = singletons_.find(descId);
            if (singletonInstance != nullptr) {
                return MaterializedBeanHandle::direct(
                    singletonInstance,
                    kInvalidSlotId,
                    descId);
            }
        }

        DescriptorId materializationDescId = descId;

        // Single Descriptor& reference reused in the alias block and the switch.
        const Descriptor &desc = descriptors_.at(descId);

        // ── Polymorphic alias: redirect to primary then apply adjustToExposed ────
        {
            if (desc.primaryDescriptor != descId
                && desc.primaryDescriptor != kInvalidDescriptorId) {
                const DescriptorId primaryId = desc.primaryDescriptor;

                if (lt == Lifetime::Singleton) {
                    // Fast path: primary already materialized.
                    void *concretePtr = singletons_.find(primaryId);
                    if (concretePtr == nullptr) {
                        // Slow path: trigger primary materialization through the shared path.
                        concretePtr = materializeOneImpl(primaryId, ctx).instance();
                    }
                    void *exposedPtr = concretePtr;
                    if (desc.adjustToExposed != nullptr)
                        exposedPtr = desc.adjustToExposed(concretePtr);
                    return MaterializedBeanHandle::direct(
                        exposedPtr,
                        kInvalidSlotId,
                        descId);
                }

                if (lt == Lifetime::Prototype) {
                    const Descriptor &primaryDesc = descriptors_.at(primaryId);
                    CycleGuard guard(materializationStack(), primaryId);

                    void *mem;
                    SlotId slotId;
                    if (primaryDesc.allocAndConstruct != nullptr) {
                        mem = primaryDesc.allocAndConstruct(static_cast<void *>(&ctx));
                        slotId = prototypeStore().allocate(primaryId, mem);
                    } else {
                        mem = ::operator new(primaryDesc.size, std::align_val_t{primaryDesc.align});
                        slotId = prototypeStore().allocate(primaryId, mem);
                        try {
                            primaryDesc.construct(mem, static_cast<void *>(&ctx));
                        } catch (...) {
                            ::operator delete(
                                mem,
                                primaryDesc.size,
                                std::align_val_t{primaryDesc.align}
                                );
                            prototypeStore().reclaim(slotId);
                            throw;
                        }
                    }
                    prototypeStore().activate(slotId);

                    void *exposedPtr = mem;
                    if (desc.adjustToExposed != nullptr)
                        exposedPtr = desc.adjustToExposed(mem);

                    const bool dispatchInitialized =
                        listeners_.hasListeners(ListenerStore::phaseInitialized());
                    const bool dispatchCreated =
                        listeners_.hasListeners(ListenerStore::phaseCreated());
                    if (dispatchInitialized || dispatchCreated) {
                        ctr::AnyBean anyBean;
                        anyBean.object_ = exposedPtr;
                        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                        anyBean.bits_.f1.descId = descId;
                        anyBean.registry_ = prototypeStoreBlock();
                        anyBean.retainIfPrototype();

                        if (dispatchInitialized) {
                            listeners_.dispatch(
                                ListenerStore::phaseInitialized(),
                                desc.exposedType,
                                &anyBean
                                );
                        }
                        if (primaryDesc.postConstruct) {
                            primaryDesc.postConstruct(mem, static_cast<void *>(&ctx));
                        }
                        if (dispatchCreated) {
                            listeners_.dispatch(
                                ListenerStore::phaseCreated(),
                                desc.exposedType,
                                &anyBean
                                );
                        }
                    } else if (primaryDesc.postConstruct) {
                        primaryDesc.postConstruct(mem, static_cast<void *>(&ctx));
                    }
                    return MaterializedBeanHandle::direct(exposedPtr, slotId, descId);
                }

                materializationDescId = primaryId;
            }
        }
        // ── End alias redirect ───────────────────────────────────────────────────

        switch (lt) {
        case Lifetime::Singleton: {
            // Fast path: lock-free after start().
            void *instance = singletonInstance;

            if (instance == nullptr) {
                bool didMaterialize = false;

                // ── Phase 1: claim descId or wait for a peer to complete ───
                didMaterialize = claimMaterializationOrWait(
                    descId,
                    nullptr,
                    [&] { return singletons_.find(descId); },
                    "Registry::resolve: dependency cycle "
                    "detected during singleton materialization.",
                    "Registry::resolve: cross-thread dependency cycle "
                    "detected during singleton materialization."
                    );
                if (!didMaterialize) {
                    instance = singletons_.find(descId);
                }

                // ── Phase 2: construct outside the lock ───────────────────
                if (didMaterialize) {
                    void *mem = nullptr;
                    // RuntimeBinding singletons are pre-stored before start() or via
                    // growAndStore() in bindSingleton(). Reaching this allocation path
                    // is an internal invariant violation.
                    assert(desc.origin != Origin::RuntimeBinding);
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
                                ::operator delete(
                                    mem,
                                    desc.size,
                                    std::align_val_t{desc.align}
                                    );
                            }
                        }
                        {
                            std::lock_guard reLock(writeLock_);
                            auto it = std::find_if(
                                materializing_.begin(),
                                materializing_.end(),
                                [descId](const MaterializingEntry& p) {
                                    return p.key.descId == descId && p.key.scope == nullptr;
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

                    // ── Phase 3: store under lock, then wake waiters ───────
                    {
                        std::lock_guard reLock(writeLock_);
                        singletons_.store(descId, mem);
                        auto it = std::find_if(
                            materializing_.begin(),
                            materializing_.end(),
                            [descId](const MaterializingEntry& p) {
                                return p.key.descId == descId && p.key.scope == nullptr;
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
                }

                // Lifecycle dispatch outside the write lock (specs-api §13.1):
                //   C++ construction → onInitialized → postConstruct → onCreated
                if (didMaterialize) {
                    ctr::AnyBean anyBean;
                    anyBean.object_ = instance;
                    anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
                    anyBean.bits_.f1.descId = descId;
                    anyBean.registry_ = this;

                    listeners_.dispatch(
                        ListenerStore::phaseInitialized(),
                        desc.exposedType,
                        &anyBean
                        );
                    if (desc.postConstruct) {
                        desc.postConstruct(instance, static_cast<void *>(&ctx));
                    }
                    listeners_.dispatch(
                        ListenerStore::phaseCreated(),
                        desc.exposedType,
                        &anyBean
                        );
                }
            }

            // Return Direct Form 1 (kInvalidSlotId = singleton, no refcount).
            return MaterializedBeanHandle::direct(instance, kInvalidSlotId, descId);
        }

        case Lifetime::Prototype: {
            CycleGuard guard(materializationStack(), descId);

            void *mem;
            SlotId slotId;

            if (desc.allocAndConstruct != nullptr) {
                mem = desc.allocAndConstruct(static_cast<void *>(&ctx));
                slotId = prototypeStore().allocate(descId, mem);
            } else {
                mem = ::operator new(desc.size, std::align_val_t{desc.align});
                slotId = prototypeStore().allocate(descId, mem);
                try {
                    desc.construct(mem, static_cast<void *>(&ctx));
                } catch (...) {
                    ::operator delete(mem, desc.size, std::align_val_t{desc.align});
                    prototypeStore().reclaim(slotId);
                    throw;
                }
            }

            prototypeStore().activate(slotId);

            const bool dispatchInitialized =
                listeners_.hasListeners(ListenerStore::phaseInitialized());
            const bool dispatchCreated =
                listeners_.hasListeners(ListenerStore::phaseCreated());
            if (dispatchInitialized || dispatchCreated) {
                ctr::AnyBean anyBean;
                anyBean.object_ = mem;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                anyBean.bits_.f1.descId = descId;
                anyBean.registry_ = prototypeStoreBlock();
                anyBean.retainIfPrototype(); // refcount: 1 → 2 (dispatch reference)

                if (dispatchInitialized) {
                    listeners_.dispatch(
                        ListenerStore::phaseInitialized(),
                        desc.exposedType,
                        &anyBean
                        );
                }
                if (desc.postConstruct) {
                    desc.postConstruct(mem, static_cast<void *>(&ctx));
                }
                if (dispatchCreated) {
                    listeners_.dispatch(
                        ListenerStore::phaseCreated(),
                        desc.exposedType,
                        &anyBean
                        );
                }
            } else if (desc.postConstruct) {
                desc.postConstruct(mem, static_cast<void *>(&ctx));
            }

            return MaterializedBeanHandle::direct(mem, slotId, descId);
        }

        case Lifetime::Session: {
            if (ctx.scope == nullptr) {
                throw ctr::ContextStateError(
                    "Registry::resolve: session beans can only be resolved from a "
                    "scoped context, not directly from a root context."
                    );
            }
            if (!ctx.scope->scopeStarted_) {
                throw ctr::ContextStateError(
                    "Registry::resolve: the scope is stopped; start the scope before resolving."
                    );
            }
            (void)materializeSessionInstance(materializationDescId, ctx.scope, ctx);
            return MaterializedBeanHandle::proxy(ctx.scope->scopeNameId_, descId);
        }

        case Lifetime::ThreadLocal: {
            // Scope is transparent for threadLocal (specs-api §8): always resolve
            // as if from root — ctx.scope is ignored.
            (void)materializeThreadLocalInstance(materializationDescId, ctx);
            return MaterializedBeanHandle::threadLocal(descId);
        }
        }

        // Unreachable; suppress compiler warnings.
        throw ctr::ConfigurationError("Registry::resolve: unhandled lifetime.");
    }


} // namespace ctr::detail
