#pragma once

#ifndef CTORIUM_DYNAMIC_LINK

#include <shared_mutex>
#include "Registry.hpp"
#include "BeanDescriptorGen.hpp"
#include "HashUtils.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

namespace ctr {

// -------------------------------------------------------------------------
// Process-wide root registry
// -------------------------------------------------------------------------

// Heterogeneous map: resolveContext(string_view) performs no heap allocation on hit.
inline std::unordered_map<std::string, std::unique_ptr<BeanContext>,
                           detail::StringViewHash, std::equal_to<>> g_roots;
inline std::mutex g_rootsMutex;

// -------------------------------------------------------------------------
// BeanContext constructors and destructor
// -------------------------------------------------------------------------

inline BeanContext::BeanContext(std::string key)
    : registry_(std::make_shared<ctr::detail::Registry>()), key_(std::move(key)) {}

inline BeanContext::BeanContext(std::shared_ptr<ctr::detail::Registry> registry)
    : registry_(std::move(registry)) {}

inline BeanContext::~BeanContext() = default;

// -------------------------------------------------------------------------
// BeanContext::assertCanDiscover_()
// -------------------------------------------------------------------------

inline void BeanContext::assertCanDiscover_() const {
    if (registry_->started()) {
        throw ContextStateError(
            "discover<>() called after start() — contributions must be "
            "submitted before the context is started.");
    }
}

// -------------------------------------------------------------------------
// BeanContext::resolveContext()
// -------------------------------------------------------------------------

inline BeanContext& BeanContext::resolveContext() {
    return resolveContext("");
}

inline BeanContext& BeanContext::resolveContext(std::string_view key) {
    std::lock_guard lock(g_rootsMutex);
    // Transparent find: no std::string allocated on hit.
    auto it = g_roots.find(key);
    if (it != g_roots.end()) return *it->second;
    auto [jt, _] = g_roots.emplace(std::string(key),
        std::unique_ptr<BeanContext>(new BeanContext(std::string(key))));
    return *jt->second;
}

// -------------------------------------------------------------------------
// BeanContext lifecycle
// -------------------------------------------------------------------------

inline BeanContext& BeanContext::start() {
    try {
        core().start(this);
        flushDeferredListeners_();
        // Dispatch lifecycle for pre-start bound singletons after deferred listeners
        // are flushed so that listeners registered before start() observe the events.
        core().dispatchBoundSingletonLifecycle();
        // Materialize eager singletons (lazy == false) after started_ is published and
        // outside the registry's internal write lock (start() already released it).
        core().materializeEagerSingletons();
        core().completeStart();
    } catch (...) {
        core().rollbackFailedStart();
        throw;
    }
    return *this;
}

inline void BeanContext::flushDeferredListeners_() {
    if (deferredListeners_.empty()) return;
    // Insert all deferred entries without rebuilding the view on each call,
    // then finalize with a single rebuild.
    for (auto& entry : deferredListeners_) {
        const detail::TypeId typeId = core().lookupTypeId(entry.typeIndex);
        (void)core().listenerStore().addListenerDeferred(
            entry.phaseIndex, typeId, std::move(entry.callback), entry.priority);
    }
    core().listenerStore().finalizeListeners();
    deferredListeners_.clear();
}

inline void BeanContext::stop() {
    core().stop();
    // Remove from the global table; the unique_ptr deletion may destroy *this.
    // No access to *this is permitted after this line.
    std::lock_guard lock(g_rootsMutex);
    g_roots.erase(key_);
}

// -------------------------------------------------------------------------
// BeanContext::resolveScope()
// -------------------------------------------------------------------------

inline ScopedContext& BeanContext::resolveScope(std::string_view key) {
    // Fast path: existing scope — check under shared lock.
    {
        std::shared_lock<std::shared_mutex> lock(scopesMutex_);
        const auto it = scopes_.find(key);
        if (it != scopes_.end()) return *it->second;
    }
    // Slow path: construct outside the lock so the shared_ptr refcount increment
    // (inside ScopedContext's constructor) does not extend the critical section.
    auto candidate = std::unique_ptr<ScopedContext>(new ScopedContext(registry_, this));
    std::unique_lock<std::shared_mutex> lock(scopesMutex_);
    // try_emplace: does NOT move candidate if key already exists.
    auto [it, inserted] = scopes_.try_emplace(std::string(key), std::move(candidate));
    if (inserted) {
        // Intern the scope name in the same NameInterning table as named-key strings
        // so that resolveScope("x") and internScopeNameCached("x") produce the same NameId.
        ScopedContext& scope = *it->second;
        scope.scopeNameId_ = core().internNameSafe(key);
        core().registerScope(scope.scopeNameId_, &scope);
    }
    return *it->second;
}

// -------------------------------------------------------------------------
// ScopedContext
// -------------------------------------------------------------------------

inline ScopedContext::ScopedContext(std::shared_ptr<ctr::detail::Registry> registry,
                                    BeanContext* root)
    : BeanContext(std::move(registry)), root_(root) {
    asScope_ = this; // Form 2 proxy path; Bean<T>::operator-> reads this via ResolutionContext
}

inline ScopedContext::~ScopedContext() {
    // Destroy pending session instances that were never started into the lifecycle.
    for (const auto& psb : pendingRuntimeSessions_) {
        const detail::Descriptor& d = core().descriptorTable().at(psb.descId);
        d.destroy(psb.instance);
        if (d.dealloc) {
            d.dealloc(psb.instance);
        } else if (d.size != 0) {
            ::operator delete(psb.instance, d.size, std::align_val_t{d.align});
        }
    }
}

inline ScopedContext& ScopedContext::start() {
    if (!core().started()) {
        throw ContextStateError(
            "ScopedContext::start: the root registry must be started before a scope can start.");
    }
    if (scopeStarted_) return *this; // idempotent
    detail::Registry& reg = core();
    sessionStore_.resize(reg.sessionSlotCount());
    // Process pending session bindings entered before this start() call.
    for (const auto& psb : pendingRuntimeSessions_) {
        const detail::Descriptor& d = reg.descriptors_.at(psb.descId);
        sessionStore_.store(d.sessionSlot, psb.descId, psb.instance);
        ctr::AnyBean anyBean;
        anyBean.object_         = psb.instance;
        anyBean.bits_.f1.slot   = static_cast<std::uint32_t>(detail::kInvalidSlotId);
        anyBean.bits_.f1.descId = psb.descId;
        anyBean.registry_       = &reg;
        reg.listeners_.dispatch(detail::ListenerStore::phaseInitialized(), d.exposedType, &anyBean);
        reg.listeners_.dispatch(detail::ListenerStore::phaseCreated(),     d.exposedType, &anyBean);
    }
    pendingRuntimeSessions_.clear();
    scopeStarted_ = true;
    return *this;
}

inline ScopedContext& ScopedContext::stop() {
    if (!scopeStarted_) return *this; // idempotent
    core().stopScope(*this); // marks stopped, destroys beans, clears store
    return *this;
}

inline ScopedContext& ScopedContext::restart() {
    stop();
    start();
    return *this;
}

inline ScopedContext& ScopedContext::resolveScope(std::string_view key) {
    return root_->resolveScope(key);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopedContext::userData / userData  (specs-api §5.4)
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
ScopedContext& ScopedContext::userData(T& value) {
    userData_     = &value;
    userDataType_ = std::type_index(typeid(T));
    return *this;
}

inline ScopedContext& ScopedContext::userData(std::nullptr_t) {
    userData_     = nullptr;
    userDataType_ = std::type_index(typeid(void));
    return *this;
}

template <class T>
std::optional<std::reference_wrapper<T>> ScopedContext::userData() {
    if (userData_ == nullptr) return std::nullopt;
    if (std::type_index(typeid(T)) != userDataType_) return std::nullopt;
    return std::ref(*static_cast<T*>(userData_));
}

template <class T>
std::optional<std::reference_wrapper<const T>> ScopedContext::userData() const {
    if (userData_ == nullptr) return std::nullopt;
    if (std::type_index(typeid(T)) != userDataType_) return std::nullopt;
    return std::cref(*static_cast<const T*>(userData_));
}

// ─────────────────────────────────────────────────────────────────────────────
// detail::deallocBoundObjectThunk<T>
//
// Deallocation-only thunk for runtime-bound objects (bindSingleton / bindSession).
// Called by executeDestructionLifecycle after ~T() already ran.
// Defined here (inside namespace ctr, nested into detail) so it is visible at
// phase-1 lookup in bindSingleton<T> and bindSession<T> template bodies.
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

template <typename T>
void deallocBoundObjectThunk(void* p) noexcept {
    ::operator delete(p);
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// BeanContext::bindSingleton<T>  — delegates to the root Registry
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
Bean<T> BeanContext::bindSingleton(std::unique_ptr<T> object, BindOptions options) {
    // Specs-api §11.1: invoked via ScopedContext → bind on the owning root.
    BeanContext* target = asScope_
        ? static_cast<ScopedContext*>(this)->root_
        : this;
    detail::Registry& reg = target->core();
    const detail::NameId nameId = reg.internNameSafe(
        options.name ? std::string_view{options.name} : std::string_view{});
    return reg.bindSingleton<T>(std::move(object), nameId, options.priority);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopedContext::bindSession<T>
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
ScopedContext& ScopedContext::bindSession(std::unique_ptr<T> object, BindOptions options) {
    if constexpr (std::is_same_v<T, ctr::BeanContext>) {
        throw ConfigurationError(
            "ScopedContext::bindSession: ctr::BeanContext cannot be bound as a "
            "session bean.");
    } else {

    // Compile-time: detect preDestroy hook (Ctorium types only).
    constexpr auto kMembers = detail::scanMembers<^^T>();
    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (kMembers.preDestroy != std::meta::info{}) {
        preDestroyFn = &detail::preDestroyThunkImpl<T, kMembers.preDestroy>;
    }
    static constexpr const char* kTypeName = detail::qualifiedNameOf(^^T);

    detail::Registry& reg = core();

    // Intern name (safe before and after start).
    const detail::NameId nameId = reg.internNameSafe(
        options.name ? std::string_view{options.name} : std::string_view{});

    std::unique_lock<std::mutex> lock(reg.writeLock_);

    detail::TypeId typeId;
    if (!reg.started_.load(std::memory_order_relaxed)) {
        // Root not started: intern the type now (TypeInterning not frozen).
        typeId = reg.typeInterning_.internByName(kTypeName, &detail::TypeInfoGetter<T>::get);
    } else {
        // Root started: unknown types are adopted through TypeInterning overflow.
        typeId = reg.lookupTypeId(std::type_index(typeid(T)));
        if (typeId == detail::kInvalidTypeId)
            typeId = reg.typeInterning_.internByName(kTypeName, &detail::TypeInfoGetter<T>::get);
    }

    // Reuse an existing RuntimeBinding session descriptor for (typeId, nameId)
    // to avoid accumulating duplicate candidates across scope cycles.
    detail::DescriptorId descId = detail::kInvalidDescriptorId;
    if (reg.started_.load(std::memory_order_relaxed)) {
        const detail::NameTable* table = reg.typeIndex_.tableFor(typeId);
        if (table) {
            auto it = table->entries.find(nameId);
            if (it != table->entries.end()) {
                for (detail::DescriptorId did : it->second.candidates) {
                    const detail::Descriptor& d = reg.descriptors_.at(did);
                    if (d.origin == detail::Origin::RuntimeBinding
                            && d.lifetime == detail::Lifetime::Session) {
                        descId = did;
                        break;
                    }
                }
            }
        }
    }

    if (descId == detail::kInvalidDescriptorId) {
        // Create new session descriptor.
        detail::Descriptor d;
        d.exposedType            = typeId;
        d.concreteType           = typeId;
        d.name                   = nameId;
        d.priority               = options.priority;
        d.lifetime               = detail::Lifetime::Session;
        d.origin                 = detail::Origin::RuntimeBinding;
        d.construct              = [](void*, void*) noexcept {};
        d.destroy                = &detail::destroyThunk<T>;
        d.postConstruct          = nullptr;
        d.preDestroy             = preDestroyFn;
        d.size                   = sizeof(T);
        d.align                  = alignof(T);
        d.allocAndConstruct      = nullptr;
        d.dealloc                = &detail::deallocBoundObjectThunk<T>;
        d.factoryMethodDescriptor = detail::kInvalidDescriptorId;
        d.observedTypeGetter     = &detail::TypeInfoGetter<T>::get;
        d.exactTypeGetter        = &detail::TypeInfoGetter<T>::get;
        d.nameStr                = kTypeName;
        if (reg.started_.load(std::memory_order_relaxed)) {
            d.sessionSlot = static_cast<detail::SessionSlot>(reg.sessionSlotCount_++);
        }
        descId = reg.descriptors_.append(std::move(d));
        reg.typeIndex_.insertCandidate(typeId, nameId, descId);
        if (reg.started_.load(std::memory_order_relaxed)) {
            reg.typeIndex_.updateSingleUnnamed(typeId);
        }
    }

    // Take ownership.
    void* rawPtr = object.release();

    // If a previous pending instance exists for this descId, replace it.
    for (auto& psb : pendingRuntimeSessions_) {
        if (psb.descId == descId) {
            const detail::Descriptor& d = reg.descriptors_.at(psb.descId);
            d.destroy(psb.instance);
            if (d.dealloc) d.dealloc(psb.instance);
            else if (d.size != 0)
                ::operator delete(psb.instance, d.size, std::align_val_t{d.align});
            psb.instance = rawPtr;
            return *this;
        }
    }

    if (!scopeStarted_) {
        // Scope not yet started (or stopped): queue for next start().
        pendingRuntimeSessions_.push_back({descId, rawPtr});
        return *this;
    }

    // Scope running: store immediately.
    const detail::SessionSlot slot = reg.descriptors_.at(descId).sessionSlot;
    sessionStore_.growAndStore(slot, descId, rawPtr);

    // A5: release writeLock_ before dispatching so listener callbacks can call
    // registry operations without deadlocking.
    lock.unlock();

    ctr::AnyBean anyBean;
    anyBean.object_         = rawPtr;
    anyBean.bits_.f1.slot   = static_cast<std::uint32_t>(detail::kInvalidSlotId);
    anyBean.bits_.f1.descId = descId;
    anyBean.registry_       = &reg;
    reg.listeners_.dispatch(detail::ListenerStore::phaseInitialized(), typeId, &anyBean);
    reg.listeners_.dispatch(detail::ListenerStore::phaseCreated(),     typeId, &anyBean);

    return *this;

    } // end else (!BeanContext)
}

// -------------------------------------------------------------------------
// detail::submitDiscoveryContribution — §8
// -------------------------------------------------------------------------

namespace detail {

template <auto... Roots>
inline void submitDiscoveryContribution(BeanContext& context, DiscoverOptions options) {
    context.assertCanDiscover_();
    if (options.retainAllMetadata) {
        static constexpr auto kDescriptors =
            std::define_static_array(makeAllDescriptors<true, Roots...>());
        context.core().submitContribution(
            std::span<const ContributedDescriptor>(kDescriptors.data(), kDescriptors.size()),
            options);
    } else {
        static constexpr auto kDescriptors =
            std::define_static_array(makeAllDescriptors<false, Roots...>());
        context.core().submitContribution(
            std::span<const ContributedDescriptor>(kDescriptors.data(), kDescriptors.size()),
            options);
    }
}

} // namespace detail

} // namespace ctr

#endif // CTORIUM_DYNAMIC_LINK
