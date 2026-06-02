#pragma once

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::resolve<T>  — hot-path resolution
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
            throw CTORIUM_NAMESPACE::ContextStateError(
                "Registry::resolve: context has not been started; "
                "call start() before resolving beans."
                );
        }

        // 2. TypeId via per-type static atomic cache (single relaxed load on hot path).
        const TypeId typeId = typeIdFor<T>();
        if (typeId == kInvalidTypeId) [[unlikely]] {
            throw CTORIUM_NAMESPACE::ResolutionError(
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
            // 4 + 5. Single flat_map lookup: precomputed head plus canonical
            // candidates for post-start bindings whose head was not precomputed.
            const NameEntry* entry = typeIndex_.entryFor(typeId, effectiveNameId);
            const DescriptorId head =
                entry != nullptr ? entry->head : kInvalidDescriptorId;
            if (head == kInvalidDescriptorId) [[unlikely]] {
                // Post-start bindings update the canonical entries table; the
                // precomputed entry head only covers candidates known at start().
                const auto* candidates = entry != nullptr ? &entry->candidates : nullptr;
                if (!candidates || candidates->empty()) {
                    throw CTORIUM_NAMESPACE::ResolutionError(
                        "Registry::resolve: no bean registered for the requested "
                        "type and named key."
                        );
                }
                descId = (*candidates)[0];
                if (candidates->size() >= 2
                    && descriptors_.at((*candidates)[0]).priority
                        == descriptors_.at((*candidates)[1]).priority) [[unlikely]] {
                    throw CTORIUM_NAMESPACE::ResolutionError(
                        "Registry::resolve: ambiguous resolution — two candidates share "
                        "the highest priority for the requested type and named key."
                        );
                }
            } else {
                descId = head;
                if (entry->ambiguous) [[unlikely]] {
                    throw CTORIUM_NAMESPACE::ResolutionError(
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
                throw CTORIUM_NAMESPACE::ContextStateError(
                    "Registry::resolve: session beans can only be resolved from a "
                    "scoped context, not directly from a root context."
                    );
            }
            if (ctx.scope->scopeState_ == CTORIUM_NAMESPACE::ScopedContext::ScopeState::Stopped) {
                throw CTORIUM_NAMESPACE::ContextStateError(
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
        TLCleanup& cleanup = tlCleanup();
        std::uint32_t token = cleanup.materializationThreadToken;
        if (token != kNoMaterializationThreadToken)
            return token;

        std::lock_guard lock(materializationThreadTokenMutex_);
        if (!freeMaterializationThreadTokens_.empty()) {
            token = freeMaterializationThreadTokens_.back();
            freeMaterializationThreadTokens_.pop_back();
        } else {
            token =
                nextMaterializationThreadToken_.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        cleanup.materializationThreadToken = token;
        return token;
    }

    inline void Registry::clearMaterializationWaitSlot(
        std::uint32_t threadToken
        ) noexcept {
        if (threadToken == kNoMaterializationThreadToken)
            return;

        std::lock_guard lock(writeLock_);
        if (hasMaterializationWaitSlot(threadToken))
            materializationWaitSlot(threadToken) = MaterializationWait{};
    }

    inline void Registry::recycleMaterializationThreadToken(
        std::uint32_t threadToken
        ) noexcept {
        if (threadToken == kNoMaterializationThreadToken)
            return;

        try {
            std::lock_guard lock(materializationThreadTokenMutex_);
            freeMaterializationThreadTokens_.push_back(threadToken);
        } catch (...) {
        }
    }

    inline bool Registry::hasMaterializationWaitSlot(
        std::uint32_t threadToken) const noexcept {
        return static_cast<std::size_t>(threadToken) < waitingByThreadCapacity_;
    }

    inline MaterializationWait& Registry::materializationWaitSlot(
        std::uint32_t threadToken) noexcept {
        const std::size_t index = static_cast<std::size_t>(threadToken);
        const std::size_t chunk = index / kMaterializationWaitChunkSize;
        assert(chunk < waitingByThreadChunks_.size());
        return (*waitingByThreadChunks_[chunk])[index % kMaterializationWaitChunkSize];
    }

    inline const MaterializationWait& Registry::materializationWaitSlot(
        std::uint32_t threadToken) const noexcept {
        const std::size_t index = static_cast<std::size_t>(threadToken);
        const std::size_t chunk = index / kMaterializationWaitChunkSize;
        assert(chunk < waitingByThreadChunks_.size());
        return (*waitingByThreadChunks_[chunk])[index % kMaterializationWaitChunkSize];
    }

    inline void Registry::ensureMaterializationWaitSlot(std::uint32_t threadToken) {
        std::unique_lock slotsLock(waitSlotsMutex_);
        const std::size_t requiredSlotCount =
            static_cast<std::size_t>(threadToken) + 1;
        if (requiredSlotCount <= waitingByThreadCapacity_)
            return;

        const std::size_t requiredChunkCount =
            (requiredSlotCount + kMaterializationWaitChunkSize - 1)
            / kMaterializationWaitChunkSize;

        std::vector<MaterializationWaitChunk*> grown;
        grown.reserve(requiredChunkCount);
        grown.insert(
            grown.end(),
            waitingByThreadChunks_.begin(),
            waitingByThreadChunks_.end());

        std::vector<std::unique_ptr<MaterializationWaitChunk>> newChunks;
        newChunks.reserve(requiredChunkCount - grown.size());
        while (grown.size() < requiredChunkCount) {
            auto chunk = std::make_unique<MaterializationWaitChunk>();
            grown.push_back(chunk.get());
            newChunks.push_back(std::move(chunk));
        }

        ownedWaitingByThreadChunks_.reserve(
            ownedWaitingByThreadChunks_.size() + newChunks.size());
        for (auto& chunk : newChunks)
            ownedWaitingByThreadChunks_.push_back(std::move(chunk));

        {
            std::lock_guard lock(writeLock_);
            if (requiredSlotCount <= waitingByThreadCapacity_)
                return;
            waitingByThreadChunks_.swap(grown);
            waitingByThreadCapacity_ =
                waitingByThreadChunks_.size() * kMaterializationWaitChunkSize;
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
                    || !hasMaterializationWaitSlot(token)) {
                return false;
            }
            const MaterializationWait& wait = materializationWaitSlot(token);
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
        CTORIUM_NAMESPACE::ScopedContext* scope,
        FindExisting&& findExisting,
        const char* intraThreadCycleMessage,
        const char* crossThreadCycleMessage) {
        const std::uint32_t currentThreadToken = materializationThreadToken();
        TLCleanup& cleanup = tlCleanup();
        const std::uint32_t id = registryId();
        if (cleanup.registered.find(id) == cleanup.registered.end())
            cleanup.registered.emplace(id, weak_from_this());

        ensureMaterializationWaitSlot(currentThreadToken);

        const MaterializationKey key{descId, scope};
        std::unique_lock lock(writeLock_);
        materializationWaitSlot(currentThreadToken) = MaterializationWait{};
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
            throw CTORIUM_NAMESPACE::ResolutionError(intraThreadCycleMessage);
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
                    throw CTORIUM_NAMESPACE::ResolutionError(intraThreadCycleMessage);
                }
                stack.push_back(descId);
                materializing_.push_back({key, currentThreadToken});
                return true;
            }

            const std::uint32_t ownerThreadToken = ownerIt->threadToken;
            if (ownerThreadToken == currentThreadToken) {
                lock.unlock();
                throw CTORIUM_NAMESPACE::ResolutionError(intraThreadCycleMessage);
            }
            if (!stack.empty()
                    && materializationWaitCycleDetected(
                        currentThreadToken,
                        ownerThreadToken)) {
                lock.unlock();
                throw CTORIUM_NAMESPACE::ResolutionError(crossThreadCycleMessage);
            }

            materializationWaitSlot(currentThreadToken) = MaterializationWait{key, true};
            cv_.wait(lock);
            materializationWaitSlot(currentThreadToken).active = false;
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
        const DescriptorCold &cold = descriptors_.coldAt(descId);

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
                    const DescriptorCold &primaryCold = descriptors_.coldAt(primaryId);
                    CycleGuard guard(materializationStack(), primaryId);

                    void *mem;
                    SlotId slotId;
                    if (primaryCold.allocAndConstruct != nullptr) {
                        mem = primaryCold.allocAndConstruct(static_cast<void *>(&ctx));
                        slotId = prototypeStore().allocate(primaryId, mem);
                    } else {
                        mem = ::operator new(primaryCold.size, std::align_val_t{primaryCold.align});
                        slotId = prototypeStore().allocate(primaryId, mem);
                        try {
                            primaryCold.construct(mem, static_cast<void *>(&ctx));
                        } catch (...) {
                            ::operator delete(
                                mem,
                                primaryCold.size,
                                std::align_val_t{primaryCold.align}
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
                        CTORIUM_NAMESPACE::AnyBean anyBean;
                        anyBean.object_ = exposedPtr;
                        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                        anyBean.bits_.f1.descId = descId;
                        anyBean.registry_ = prototypeStoreBlock();
                        anyBean.retainIfPrototype();

                        if (dispatchInitialized) {
                            listeners_.dispatch(
                                ListenerStore::phaseInitialized(),
                                cold.exposedType,
                                &anyBean,
                                ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
                                );
                        }
                        if (primaryCold.postConstruct) {
                            primaryCold.postConstruct(mem, static_cast<void *>(&ctx));
                        }
                        if (dispatchCreated) {
                            listeners_.dispatch(
                                ListenerStore::phaseCreated(),
                                cold.exposedType,
                                &anyBean,
                                ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
                                );
                        }
                    } else if (primaryCold.postConstruct) {
                        primaryCold.postConstruct(mem, static_cast<void *>(&ctx));
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
                    assert(cold.origin != Origin::RuntimeBinding);
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
                                ::operator delete(
                                    mem,
                                    cold.size,
                                    std::align_val_t{cold.align}
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

                // Lifecycle dispatch outside the write lock:
                //   C++ construction → onInitialized → postConstruct → onCreated
                if (didMaterialize) {
                    CTORIUM_NAMESPACE::AnyBean anyBean;
                    anyBean.object_ = instance;
                    anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
                    anyBean.bits_.f1.descId = descId;
                    anyBean.registry_ = this;

                    listeners_.dispatch(
                        ListenerStore::phaseInitialized(),
                        cold.exposedType,
                        &anyBean,
                        ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
                        );
                    if (cold.postConstruct) {
                        cold.postConstruct(instance, static_cast<void *>(&ctx));
                    }
                    listeners_.dispatch(
                        ListenerStore::phaseCreated(),
                        cold.exposedType,
                        &anyBean,
                        ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
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

            if (cold.allocAndConstruct != nullptr) {
                mem = cold.allocAndConstruct(static_cast<void *>(&ctx));
                slotId = prototypeStore().allocate(descId, mem);
            } else {
                mem = ::operator new(cold.size, std::align_val_t{cold.align});
                slotId = prototypeStore().allocate(descId, mem);
                try {
                    cold.construct(mem, static_cast<void *>(&ctx));
                } catch (...) {
                    ::operator delete(mem, cold.size, std::align_val_t{cold.align});
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
                CTORIUM_NAMESPACE::AnyBean anyBean;
                anyBean.object_ = mem;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                anyBean.bits_.f1.descId = descId;
                anyBean.registry_ = prototypeStoreBlock();
                anyBean.retainIfPrototype(); // refcount: 1 → 2 (dispatch reference)

                if (dispatchInitialized) {
                    listeners_.dispatch(
                        ListenerStore::phaseInitialized(),
                        cold.exposedType,
                        &anyBean,
                        ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
                        );
                }
                if (cold.postConstruct) {
                    cold.postConstruct(mem, static_cast<void *>(&ctx));
                }
                if (dispatchCreated) {
                    listeners_.dispatch(
                        ListenerStore::phaseCreated(),
                        cold.exposedType,
                        &anyBean,
                        ctx.scope != nullptr ? ctx.scope->scopeNameId_ : ListenerStore::kNoScope
                        );
                }
            } else if (cold.postConstruct) {
                cold.postConstruct(mem, static_cast<void *>(&ctx));
            }

            return MaterializedBeanHandle::direct(mem, slotId, descId);
        }

        case Lifetime::Session: {
            if (ctx.scope == nullptr) {
                throw CTORIUM_NAMESPACE::ContextStateError(
                    "Registry::resolve: session beans can only be resolved from a "
                    "scoped context, not directly from a root context."
                    );
            }
            if (ctx.scope->scopeState_ == CTORIUM_NAMESPACE::ScopedContext::ScopeState::Stopped) {
                throw CTORIUM_NAMESPACE::ContextStateError(
                    "Registry::resolve: the scope is stopped; start the scope before resolving."
                    );
            }
            (void)materializeSessionInstance(materializationDescId, ctx.scope, ctx);
            return MaterializedBeanHandle::proxy(ctx.scope->scopeNameId_, descId);
        }

        case Lifetime::ThreadLocal: {
            // Scope is transparent for threadLocal: always resolve
            // as if from root — ctx.scope is ignored.
            (void)materializeThreadLocalInstance(materializationDescId, ctx);
            return MaterializedBeanHandle::threadLocal(descId);
        }
        }

        // Unreachable; suppress compiler warnings.
        throw CTORIUM_NAMESPACE::ConfigurationError("Registry::resolve: unhandled lifetime.");
    }


} // namespace CTORIUM_NAMESPACE::detail
