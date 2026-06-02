#pragma once

#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>

#include "../../api/ctr/Config.hpp"

#include "DescriptorGen.hpp"
#include "Registry.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

namespace CTORIUM_NAMESPACE {

// -------------------------------------------------------------------------
// detail::deallocBoundObjectThunk<T>
//
// Deallocation-only thunk for runtime-bound objects (bindSingleton / bindSession).
// Called by executeDestructionLifecycle after ~T() already ran.
// Defined here (inside namespace ctr, nested into detail) so it is visible at
// phase-1 lookup in bindSingleton<T> and bindSession<T> template bodies.
// -------------------------------------------------------------------------

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
BeanContext& BeanContext::bindSingleton(std::unique_ptr<T> object, BindOptions options) {
    // Invoked via ScopedContext → bind on the owning root.
    BeanContext* target = asScope_
        ? static_cast<ScopedContext*>(this)->root_
        : this;
    detail::Registry& reg = target->core();
    const detail::NameId nameId = reg.internNameSafe(
        options.name ? std::string_view{options.name} : std::string_view{});
    reg.bindSingleton<T>(std::move(object), nameId, options.priority);
    return *this;
}

} // namespace CTORIUM_NAMESPACE

namespace CTORIUM_NAMESPACE::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::bindSingleton<T>
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    auto Registry::bindSingleton(
        std::unique_ptr<T> object, NameId nameId, int32_t priority
        )
        -> CTORIUM_NAMESPACE::Bean<T> {

        if constexpr (std::is_same_v<T, CTORIUM_NAMESPACE::BeanContext>) {
            throw CTORIUM_NAMESPACE::ConfigurationError(
                "Registry::bindSingleton: ctr::BeanContext is registered implicitly at "
                "start(); explicit bindSingleton<ctr::BeanContext>() is forbidden."
                );
        } else {

            // Compile-time: detect preDestroy hook (Ctorium types only).
            constexpr auto kMembers = scanMembers<^^T>();
            void (*preDestroyFn)(void *, void *) = nullptr;
            if constexpr (kMembers.preDestroy != std::meta::info{}) {
                preDestroyFn = &preDestroyThunkImpl<T, kMembers.preDestroy>;
            }
            static constexpr const char *kTypeName = qualifiedNameOf(^^T);

            std::unique_lock<std::mutex> lock(writeLock_);

            if (started_.load(std::memory_order_relaxed)) {
                // Post-start: validate before taking ownership.
                TypeId typeId = lookupTypeId(std::type_index(typeid(T)));
                if (typeId == kInvalidTypeId)
                    typeId = typeInterning_.internByName(kTypeName, &TypeInfoGetter<T>::get);
                if (candidateKeyExists(typeId, nameId, priority)) {
                    std::string message =
                        std::string("Registry::bindSingleton: type '") + kTypeName;
                    const std::string_view name = nameInterning_.nameOf(nameId);
                    if (name.empty()) {
                        message += "' unnamed binding already exists for the same priority.";
                    } else {
                        message += "' named binding '";
                        message += name;
                        message += "' already exists for the same priority.";
                    }
                    throw CTORIUM_NAMESPACE::ConfigurationError(message);
                }
                // Check concurrent materialization of the same candidate key.
                for (const MaterializingEntry& entry : materializing_) {
                    if (entry.key.scope != nullptr)
                        continue;
                    const Descriptor& materializingDesc = descriptors_.at(entry.key.descId);
                    const DescriptorCold& materializingCold =
                        descriptors_.coldAt(entry.key.descId);
                    if (materializingCold.exposedType == typeId
                            && materializingCold.name == nameId
                            && materializingDesc.priority == priority) {
                        throw CTORIUM_NAMESPACE::ConfigurationError(
                            std::string("Registry::bindSingleton: type '") + kTypeName
                            + "' is currently being materialized; binding conflicts with "
                            "concurrent materialization of the same name and priority."
                            );
                    }
                }

                // All checks passed: take ownership.
                void *rawPtr = object.release();

                Descriptor d;
                DescriptorCold cold;
                cold.exposedType = typeId;
                cold.concreteType = typeId;
                cold.name = nameId;
                d.priority = priority;
                d.lifetime = Lifetime::Singleton;
                cold.origin = Origin::RuntimeBinding;
                cold.construct = [](void *, void *) noexcept {
                };
                cold.destroy = &destroyThunk<T>;
                cold.postConstruct = nullptr;
                cold.preDestroy = preDestroyFn;
                cold.size = sizeof(T);
                cold.align = alignof(T);
                cold.allocAndConstruct = nullptr;
                cold.dealloc = &deallocBoundObjectThunk<T>;
                cold.factoryMethodDescriptor = kInvalidDescriptorId;
                cold.observedTypeGetter = &TypeInfoGetter<T>::get;
                cold.exactTypeGetter = &TypeInfoGetter<T>::get;
                cold.nameStr = kTypeName;

                const DescriptorId descId = descriptors_.append(std::move(d), std::move(cold));
                descriptors_.atMutable(descId).primaryDescriptor = descId;
                // Store instance before publishing to TypeIndex so materializeOne always
                // finds the pre-stored pointer on any path.
                singletons_.growAndStore(descId, rawPtr);
                typeIndex_.insertCandidate(typeId, nameId, descId);
                typeIndex_.sortAllCandidates(
                    [this](DescriptorId id) { return descriptors_.at(id).priority; });
                typeIndex_.updateSingleUnnamed(typeId);

                // A5: release writeLock_ before dispatching so listener callbacks can call
                // registry operations without deadlocking.
                lock.unlock();

                CTORIUM_NAMESPACE::AnyBean anyBean;
                anyBean.object_ = rawPtr;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
                anyBean.bits_.f1.descId = descId;
                anyBean.registry_ = this;
                listeners_.dispatch(
                    ListenerStore::phaseInitialized(),
                    typeId,
                    &anyBean,
                    ListenerStore::kNoScope);
                listeners_.dispatch(
                    ListenerStore::phaseCreated(),
                    typeId,
                    &anyBean,
                    ListenerStore::kNoScope);

                return CTORIUM_NAMESPACE::Bean<T>::makeDirect(static_cast<T *>(rawPtr), kInvalidSlotId, descId, this);
            }

            for (const PendingRuntimeSingleton& pending : pendingRuntimeSingletons_) {
                if (std::string_view{pending.typeName} == std::string_view{kTypeName}
                        && pending.nameId == nameId
                        && pending.priority == priority) {
                    throw CTORIUM_NAMESPACE::ConfigurationError(
                        std::string("Registry::bindSingleton: type '") + kTypeName
                        + "' binding already exists for the same name and priority."
                        );
                }
            }

            // Pre-start: enqueue for processing in start() Phase 1.5.
            void *rawPtr = object.release();
            pendingRuntimeSingletons_.push_back(
                {
                    rawPtr, kTypeName, &TypeInfoGetter<T>::get, nameId, priority,
                    &destroyThunk<T>, preDestroyFn, &deallocBoundObjectThunk<T>,
                    sizeof(T), alignof(T)
                }
                );
            return CTORIUM_NAMESPACE::Bean<T>{}; // empty handle — use resolve<T>() after start()

        } // end else (!BeanContext)
    }

} // namespace CTORIUM_NAMESPACE::detail

namespace CTORIUM_NAMESPACE {
// ─────────────────────────────────────────────────────────────────────────────
// ScopedContext::bindSession<T>
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
ScopedContext& ScopedContext::bindSession(std::unique_ptr<T> object, BindOptions options) {
    if constexpr (std::is_same_v<T, CTORIUM_NAMESPACE::BeanContext>) {
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

    for (const auto& psb : pendingRuntimeSessions_) {
        const detail::Descriptor& pendingDesc = reg.descriptors_.at(psb.descId);
        const detail::DescriptorCold& pendingCold = reg.descriptors_.coldAt(psb.descId);
        if (pendingCold.exposedType == typeId
                && pendingCold.name == nameId
                && pendingDesc.priority == options.priority) {
            throw ConfigurationError(
                "ScopedContext::bindSession: binding already exists for this "
                "session key in the current scope cycle."
                );
        }
    }

    // Reuse an existing RuntimeBinding session descriptor for (typeId, nameId, priority)
    // to avoid accumulating duplicate candidates across scope cycles.
    detail::DescriptorId descId = detail::kInvalidDescriptorId;
    if (reg.started_.load(std::memory_order_relaxed)) {
        const detail::NameTable* table = reg.typeIndex_.tableFor(typeId);
        if (table) {
            auto it = table->entries.find(nameId);
            if (it != table->entries.end()) {
                for (detail::DescriptorId did : it->second.candidates) {
                    const detail::Descriptor& d = reg.descriptors_.at(did);
                    const detail::DescriptorCold& cold = reg.descriptors_.coldAt(did);
                    if (cold.origin == detail::Origin::RuntimeBinding
                            && d.lifetime == detail::Lifetime::Session
                            && d.priority == options.priority) {
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
        detail::DescriptorCold cold;
        cold.exposedType         = typeId;
        cold.concreteType        = typeId;
        cold.name                = nameId;
        d.priority               = options.priority;
        d.lifetime               = detail::Lifetime::Session;
        cold.origin              = detail::Origin::RuntimeBinding;
        cold.construct           = [](void*, void*) noexcept {};
        cold.destroy             = &detail::destroyThunk<T>;
        cold.postConstruct       = nullptr;
        cold.preDestroy          = preDestroyFn;
        cold.size                = sizeof(T);
        cold.align               = alignof(T);
        cold.allocAndConstruct   = nullptr;
        cold.dealloc             = &detail::deallocBoundObjectThunk<T>;
        cold.factoryMethodDescriptor = detail::kInvalidDescriptorId;
        cold.observedTypeGetter  = &detail::TypeInfoGetter<T>::get;
        cold.exactTypeGetter     = &detail::TypeInfoGetter<T>::get;
        cold.nameStr             = kTypeName;
        if (reg.started_.load(std::memory_order_relaxed)) {
            d.sessionSlot = static_cast<detail::SessionSlot>(reg.sessionSlotCount_++);
        }
        descId = reg.descriptors_.append(std::move(d), std::move(cold));
        reg.descriptors_.atMutable(descId).primaryDescriptor = descId;
        reg.typeIndex_.insertCandidate(typeId, nameId, descId);
        if (reg.started_.load(std::memory_order_relaxed)) {
            reg.typeIndex_.sortAllCandidates(
                [&reg](detail::DescriptorId id) { return reg.descriptors_.at(id).priority; });
            reg.typeIndex_.updateSingleUnnamed(typeId);
        }
    }

    // If a previous pending instance exists for this descId, reject it before
    // taking ownership so the incoming unique_ptr releases its memory on throw.
    for (auto& psb : pendingRuntimeSessions_) {
        if (psb.descId == descId) {
            throw ConfigurationError(
                "ScopedContext::bindSession: binding already exists for this "
                "session key in the current scope cycle."
                );
        }
    }

    if (scopeState_ != ScopeState::Running) {
        // Take ownership.
        void* rawPtr = object.release();
        // Scope not yet started (or stopped): queue for next start().
        pendingRuntimeSessions_.push_back({descId, rawPtr});
        return *this;
    }

    // Scope running: store immediately.
    const detail::SessionSlot slot = reg.descriptors_.at(descId).sessionSlot;
    if (sessionStore_.find(slot) != nullptr) {
        throw ConfigurationError(
            "ScopedContext::bindSession: binding already exists for this "
            "session key in the current scope cycle."
            );
    }
    // Take ownership.
    void* rawPtr = object.release();
    sessionStore_.growAndStore(slot, descId, rawPtr);

    // A5: release writeLock_ before dispatching so listener callbacks can call
    // registry operations without deadlocking.
    lock.unlock();

    CTORIUM_NAMESPACE::AnyBean anyBean;
    anyBean.object_         = rawPtr;
    anyBean.bits_.f1.slot   = static_cast<std::uint32_t>(detail::kInvalidSlotId);
    anyBean.bits_.f1.descId = descId;
    anyBean.registry_       = &reg;
    reg.listeners_.dispatch(
        detail::ListenerStore::phaseInitialized(),
        typeId,
        &anyBean,
        scopeNameId_);
    reg.listeners_.dispatch(
        detail::ListenerStore::phaseCreated(),
        typeId,
        &anyBean,
        scopeNameId_);

    return *this;

    } // end else (!BeanContext)
}

} // namespace CTORIUM_NAMESPACE
