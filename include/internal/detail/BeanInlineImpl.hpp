#pragma once

#ifndef CTORIUM_DYNAMIC_LINK

#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "Registry.hpp"
#include "ResolutionContext.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/AnyBean.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Errors.hpp"

namespace ctr {

// ─────────────────────────────────────────────────────────────────────────────
// Bean<T> prototype refcount helpers
//
// Singletons carry kInvalidSlotId — no refcount management needed.
// Prototype refcount operations are deferred to the prototype spec.
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
void Bean<T>::retainIfPrototype() noexcept {
    if (object_ == nullptr) return;                           // Form 2 or empty
    if (bits_.f1.slot == detail::kInvalidSlotId) return;     // singleton
    if (!registry_->startedRelaxed()) return;                 // context stopped
    registry_->prototypeStore().retain(bits_.f1.slot);
}

template <class T>
void Bean<T>::releaseIfPrototype() noexcept {
    if (object_ == nullptr) return;                           // Form 2 or empty
    if (bits_.f1.slot == detail::kInvalidSlotId) return;     // singleton
    if (!registry_->startedRelaxed()) return;                 // context stopped; stop() already ran
    if (!registry_->prototypeStore().releaseAcquire(bits_.f1.slot)) return;
    // Single metas_[slot] access instead of separate pointerAt + descriptorIdAt.
    const auto [mem, descId] =
        registry_->prototypeStore().slotMetaAt(bits_.f1.slot);
    registry_->executeDestructionLifecycle(descId, mem);
    registry_->prototypeStore().reclaimSlot(bits_.f1.slot);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeanContext::resolve<T>()  — unnamed resolution
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
Bean<T> BeanContext::resolve() {
    detail::Registry& reg = core();
    detail::ResolutionContext ctx{reg};
    return reg.resolve<T>(detail::kUnnamed, ctx);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeanContext::resolve<T>(named)  — named resolution
//
// Uses lookup() (not intern()) — resolution must not mutate the name table.
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
Bean<T> BeanContext::resolve(named key) {
    detail::Registry& reg = core();
    const detail::NameId nameId = reg.nameInterning().lookup(key.name);
    if (nameId == detail::kInvalidNameId) {
        throw ResolutionError(
            std::string("BeanContext::resolve: unknown named qualifier '")
            + std::string(key.name) + "'.");
    }
    detail::ResolutionContext ctx{reg};
    return reg.resolve<T>(nameId, ctx);
}

} // namespace ctr

namespace ctr::detail {

// ─────────────────────────────────────────────────────────────────────────────
// Registry::executeDestructionLifecycle
// ─────────────────────────────────────────────────────────────────────────────

inline void Registry::executeDestructionLifecycle(DescriptorId descId, void* mem) noexcept {
    const Descriptor& d = descriptors_.at(descId);
    ResolutionContext ctx{*this};

    ctr::AnyBean anyBean;
    anyBean.object_       = mem;
    anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
    anyBean.registry_     = this;

    listeners_.dispatch(ListenerStore::phasePreDestroy(), d.exposedType, &anyBean);

    if (d.preDestroy) {
        d.preDestroy(mem, static_cast<void*>(&ctx));
    }

    d.destroy(mem);

    // anyBean.object_ points to freed memory from here; listeners must not dereference it.
    listeners_.dispatch(ListenerStore::phaseDestroyed(), d.exposedType, &anyBean);

    if (d.dealloc != nullptr) {
        d.dealloc(mem);
    } else if (d.size != 0) {
        ::operator delete(mem, d.size, std::align_val_t{d.align});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry::registerContextBean
// ─────────────────────────────────────────────────────────────────────────────

inline void Registry::registerContextBean(BeanContext* ctx) {
    const TypeId typeId = typeInterning_.internByName(
        "ctr::BeanContext", TypeInfoGetter<ctr::BeanContext>::get);

    Descriptor d;
    d.exposedType             = typeId;
    d.concreteType            = typeId;
    d.name                    = kUnnamed;
    d.priority                = 0;
    d.lifetime                = Lifetime::Singleton;
    d.origin                  = Origin::RuntimeBinding;
    d.construct               = [](void*, void*) noexcept {};
    d.destroy                 = [](void*) noexcept {};
    d.postConstruct           = nullptr;
    d.preDestroy              = nullptr;
    d.size                    = 0; // externally owned — executeDestructionLifecycle skips ::operator delete
    d.align                   = alignof(ctr::BeanContext);
    d.factoryMethodDescriptor = kInvalidDescriptorId;

    beanContextDescId_ = descriptors_.append(std::move(d));
    typeIndex_.insertCandidate(typeId, kUnnamed, beanContextDescId_);

    singletons_.resize(descriptors_.size()); // extend one slot for BeanContext
    singletons_.store(beanContextDescId_, static_cast<void*>(ctx));

    ctr::AnyBean anyBean;
    anyBean.object_       = ctx;
    anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
    anyBean.registry_     = this;

    listeners_.dispatch(ListenerStore::phaseInitialized(), typeId, &anyBean);
    listeners_.dispatch(ListenerStore::phaseCreated(),     typeId, &anyBean);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry::resolve<T>  — hot-path singleton resolution (specs-internal §9)
//
// Lifetime coverage in this iteration: Singleton only.
// Prototype / Session / ThreadLocal → ConfigurationError stub.
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
// Concurrent threads that encounter a descId in materializing_ spin with
// std::this_thread::yield() until the materializing thread completes.
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
auto Registry::resolve(NameId nameId, ResolutionContext& ctx) -> Bean<T> {
    // 1. State check — acquire pairs with the release store in start(), ensuring all
    // structures populated during start() are visible to this resolving thread.
    if (!started_.load(std::memory_order_acquire)) [[unlikely]] {
        throw ctr::ContextStateError(
            "Registry::resolve: context has not been started; "
            "call start() before resolving beans.");
    }

    // 2. TypeId via per-type static atomic cache (single relaxed load on hot path).
    const TypeId typeId = typeIdFor<T>();
    if (typeId == kInvalidTypeId) [[unlikely]] {
        throw ctr::ResolutionError(
            "Registry::resolve: the requested type was not registered in "
            "this context via discover<>() or bindSingleton().");
    }

    // Fast-path: mono-candidate unnamed, no defaults configured.
    // Skips the NameTable → flat_map → vector chain and the ambiguity check.
    DescriptorId descId;
    if (nameId == kUnnamed && !hasAnyDefault_.load(std::memory_order_relaxed)) [[likely]] {
        descId = typeIndex_.singleUnnamedCandidate(typeId);
        if (descId != kInvalidDescriptorId) [[likely]] {
            // Single unnamed candidate confirmed — fall through to materialization.
            goto materialize;
        }
    }

    {
        // 3+6. Apply default NameId for unnamed resolution (specs-internal §9 step 6).
        const NameId effectiveNameId =
            (nameId == kUnnamed && hasAnyDefault_.load(std::memory_order_relaxed))
            ? defaults_.getDefault(typeId) : nameId;

        // 4. Candidate lookup.
        const std::vector<DescriptorId>* candidates =
            typeIndex_.candidatesFor(typeId, effectiveNameId);
        if (!candidates || candidates->empty()) [[unlikely]] {
            throw ctr::ResolutionError(
                "Registry::resolve: no bean registered for the requested "
                "type and named key.");
        }

        // 5. Priority arbitration — ambiguity state precalculated at start(), no
        // second descriptors_.at() needed on the hot path.
        descId = (*candidates)[0];
        if (typeIndex_.isAmbiguousFor(typeId, effectiveNameId)) [[unlikely]] {
            throw ctr::ResolutionError(
                "Registry::resolve: ambiguous resolution — two candidates share "
                "the highest priority for the requested type and named key.");
        }
    }

    materialize:
    const Lifetime lt = descriptors_.lifetimeOf(descId);

    switch (lt) {
        case Lifetime::Singleton: {
            // Fast path: lock-free after start().
            void* instance = singletons_.find(descId);

            if (instance == nullptr) {
                const Descriptor& desc = descriptors_.at(descId);
                bool didMaterialize = false;

                // ── Phase 1: claim descId or wait for a peer to complete ───
                {
                    std::unique_lock lock(writeLock_);
                    // Wait until (a) peer completed the singleton or (b) slot free.
                    cv_.wait(lock, [&] {
                        return singletons_.find(descId) != nullptr
                            || std::find(materializing_.begin(),
                                         materializing_.end(), descId)
                               == materializing_.end();
                    });
                    instance = singletons_.find(descId);
                    if (instance == nullptr) {
                        // Cycle detection (materializationStack() is thread_local).
                        for (DescriptorId existing : materializationStack()) {
                            if (existing == descId) {
                                throw ctr::ResolutionError(
                                    "Registry::resolve: dependency cycle "
                                    "detected during singleton materialization.");
                            }
                        }
                        materializationStack().push_back(descId);
                        materializing_.push_back(descId);
                        didMaterialize = true;
                    }
                }

                // ── Phase 2: construct outside the lock ───────────────────
                if (didMaterialize) {
                    void* mem = nullptr;
                    try {
                        mem = ::operator new(desc.size, std::align_val_t{desc.align});
                        desc.construct(mem, static_cast<void*>(&ctx));
                    } catch (...) {
                        if (mem) {
                            ::operator delete(mem, desc.size,
                                              std::align_val_t{desc.align});
                        }
                        {
                            std::lock_guard reLock(writeLock_);
                            auto it = std::find(materializing_.begin(),
                                                materializing_.end(), descId);
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
                        auto it = std::find(materializing_.begin(),
                                            materializing_.end(), descId);
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
                    anyBean.object_       = instance;
                    anyBean.bits_.f1.slot =
                        static_cast<std::uint32_t>(kInvalidSlotId);
                    anyBean.registry_     = this;

                    listeners_.dispatch(ListenerStore::phaseInitialized(),
                                        desc.exposedType, &anyBean);
                    if (desc.postConstruct) {
                        desc.postConstruct(instance, static_cast<void*>(&ctx));
                    }
                    listeners_.dispatch(ListenerStore::phaseCreated(),
                                        desc.exposedType, &anyBean);
                }
            }

            // 8. Return Bean<T> Form 1 (kInvalidSlotId = singleton, no refcount)
            return ctr::Bean<T>::makeDirect(
                static_cast<T*>(instance), kInvalidSlotId, this);
        }

        case Lifetime::Prototype: {
            const Descriptor& desc = descriptors_.at(descId);
            // Every resolve<T>() call creates a new instance — no caching.
            // CycleGuard uses the thread_local materializationStack() (same as
            // singleton). Prototype cycle detection is per-thread and lock-free.
            CycleGuard guard(materializationStack(), descId);

            void* mem;
            SlotId slotId;

            if (desc.allocAndConstruct != nullptr) {
                // Fast path for unique_ptr<T> factory products: allocation and
                // construction happen inside the thunk (single allocation).
                // On exception from the factory: unique_ptr destructor cleans up
                // automatically — no memory to free here.
                mem = desc.allocAndConstruct(static_cast<void*>(&ctx));
                // Phase 1 — register the already-constructed instance.
                slotId = prototypes_.allocate(descId, mem);
            } else {
                // Standard path: pre-allocate, then construct separately.
                mem = ::operator new(desc.size, std::align_val_t{desc.align});
                // Phase 1 — acquire slot.
                slotId = prototypes_.allocate(descId, mem);
                // Phase 2 — without lock: invoke construct thunk.
                // On exception: free pre-allocated memory and reclaim the slot.
                try {
                    desc.construct(mem, static_cast<void*>(&ctx));
                } catch (...) {
                    ::operator delete(mem, desc.size, std::align_val_t{desc.align});
                    prototypes_.reclaim(slotId);
                    throw;
                }
            }

            // Phase 3 — activate: set refcount to 1.
            // No lock needed: the slot is not yet reachable by other threads.
            prototypes_.activate(slotId);

            // Lifecycle dispatch (specs-api §13.1):
            //   C++ construction → onInitialized → postConstruct → onCreated
            {
                ctr::AnyBean anyBean;
                anyBean.object_       = mem;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                anyBean.registry_     = this;

                listeners_.dispatch(ListenerStore::phaseInitialized(),
                                    desc.exposedType, &anyBean);
                if (desc.postConstruct) {
                    desc.postConstruct(mem, static_cast<void*>(&ctx));
                }
                listeners_.dispatch(ListenerStore::phaseCreated(),
                                    desc.exposedType, &anyBean);
            }

            return ctr::Bean<T>::makeDirect(
                static_cast<T*>(mem), slotId, this);
        }

        case Lifetime::Session:
        case Lifetime::ThreadLocal:
            throw ctr::ConfigurationError(
                "Registry::resolve: Session and ThreadLocal lifetimes are "
                "not yet implemented.");
    }

    // Unreachable; suppress compiler warnings.
    throw ctr::ConfigurationError("Registry::resolve: unhandled lifetime.");
}

} // namespace ctr::detail

#endif // CTORIUM_DYNAMIC_LINK
