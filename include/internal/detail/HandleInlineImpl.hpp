#pragma once

#include <algorithm>
#include <cassert>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "Registry.hpp"
#include "ResolutionContext.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/AnyBean.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/BeanMetadata.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

namespace ctr {
    template <class T>
    Bean<T> Bean<T>::makeDirect(
        T *instance, detail::SlotId slot,
        detail::DescriptorId descId,
        detail::Registry *reg
        ) noexcept {
        Bean b;
        b.object_ = instance;
        b.bits_.f1.slot = slot;
        b.bits_.f1.descId = descId;
        b.registry_ = slot != detail::kInvalidSlotId
            ? static_cast<void*>(reg->prototypeStoreBlock())
            : static_cast<void*>(reg);
        return b;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T> prototype refcount helpers
    //
    // Singletons carry kInvalidSlotId — no refcount management needed.
    // Prototype tracking call-site guard contract: these helpers are inlined,
    // and the empty/singleton fast-exit is hoisted into each caller as an
    // explicit prototype-form guard.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    [[gnu::always_inline]] inline void Bean<T>::retainIfPrototype() noexcept {
        auto* store = static_cast<detail::PrototypeStore*>(registry_);
        store->retain(bits_.f1.slot);
    }

    template <class T>
    inline void Bean<T>::releaseIfPrototype() noexcept {
        auto* store = static_cast<detail::PrototypeStore*>(registry_);
        if (!store->releaseAcquire(bits_.f1.slot))
            return;
        [[unlikely]] {
            if (!store->alive.load(std::memory_order_acquire)) {
                store->reclaimSlot(bits_.f1.slot);
                return;
            }
            store->registry->releasePrototypeLast(bits_.f1.slot, store);
            return;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // AnyBean prototype refcount helpers
    // ─────────────────────────────────────────────────────────────────────────────

    [[gnu::always_inline]] inline void ctr::AnyBean::retainIfPrototype() noexcept {
        auto* store = static_cast<detail::PrototypeStore*>(registry_);
        store->retain(bits_.f1.slot);
    }

    inline void ctr::AnyBean::releaseIfPrototype() noexcept {
        auto* store = static_cast<detail::PrototypeStore*>(registry_);
        if (!store->releaseAcquire(bits_.f1.slot))
            return;
        [[unlikely]] {
            if (!store->alive.load(std::memory_order_acquire)) {
                store->reclaimSlot(bits_.f1.slot);
                return;
            }
            store->registry->releasePrototypeLast(bits_.f1.slot, store);
            return;
        }
    }


    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T> — context, exact, compatible, cast, tryCast
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    detail::Registry* Bean<T>::registry() const noexcept {
        if (bits_.f1.slot != detail::kInvalidSlotId && object_ != nullptr)
            return static_cast<detail::PrototypeStore*>(registry_)->registry;
        return static_cast<detail::Registry*>(registry_);
    }

    template <class T>
    BeanContext &Bean<T>::context() const noexcept {
        detail::Registry* reg = registry();
        if (object_ != nullptr) {
            // Form 1: singleton/prototype — owned by root.
            return *reg->rootContext();
        }
        // Form 2: session handle — return the owning ScopedContext.
        ctr::ScopedContext *scope = reg->findScope(bits_.f2.scopeNameId);
        if (scope != nullptr)
            return *scope;
        return *reg->rootContext(); // fallback (scope no longer registered)
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T>::proxyResolve_()  — Form 2 proxy path
    //
    // Algorithm:
    //   1. Look up scope by f2.scopeNameId.
    //   2. Scope absent or stopped → nullptr (no exception).
    //   3. Find or materialize the primary descriptor's session slot.
    //   4. Return the pointer adjusted to f2.descId's exposed type.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    T *Bean<T>::proxyResolve_() const noexcept {
        auto* reg = static_cast<detail::Registry*>(registry_);
        ctr::ScopedContext *scope = reg->findScope(bits_.f2.scopeNameId);
        if (scope == nullptr || !scope->scopeStarted_)
            return nullptr;

        const detail::DescriptorId descId = bits_.f2.descId;
        const detail::Descriptor &desc = reg->descriptorTable().at(descId);
        const detail::DescriptorId primaryId =
            desc.primaryDescriptor != detail::kInvalidDescriptorId
                ? desc.primaryDescriptor
                : descId;
        const detail::Descriptor &primaryDesc =
            primaryId == descId ? desc : reg->descriptorTable().at(primaryId);
        const detail::SessionSlot slot = primaryDesc.sessionSlot;

        void *instance = scope->sessionStore_.find(slot);
        if (instance != nullptr) {
            if (desc.adjustToExposed != nullptr)
                instance = desc.adjustToExposed(instance);
            return static_cast<T *>(instance);
        }

        detail::ResolutionContext ctx{*reg, scope};
        try {
            instance = reg->materializeSessionInstance(descId, scope, ctx);
        } catch (...) {
            return nullptr;
        }
        if (desc.adjustToExposed != nullptr)
            instance = desc.adjustToExposed(instance);
        return static_cast<T *>(instance);
    }

    template <class T>
    template <class U>
    bool Bean<T>::exact() const noexcept {
        detail::Registry* reg = registry();
        if (reg == nullptr)
            return false;
        const detail::TypeId tid = reg->typeIdFor<U>();
        if (tid == detail::kInvalidTypeId)
            return false;
        if (object_ != nullptr) {
            return reg->descriptorTable().coldAt(bits_.f1.descId).concreteType == tid;
        }
        return reg->descriptorTable().coldAt(bits_.f2.descId).concreteType == tid;
    }

    // Helper: search TypeIndex for a descriptor of type `uid` with the given primary.
    // Returns kInvalidDescriptorId if not found.
    static detail::DescriptorId findAliasByPrimary(
        detail::Registry *reg, detail::TypeId uid,
        detail::NameId name, detail::DescriptorId primary
        ) noexcept {
        auto check = [&](const std::vector<detail::DescriptorId> *cands) -> detail::DescriptorId {
            if (!cands)
                return detail::kInvalidDescriptorId;
            for (detail::DescriptorId did : *cands) {
                if (reg->descriptorTable().at(did).primaryDescriptor == primary)
                    return did;
            }
            return detail::kInvalidDescriptorId;
        };
        detail::DescriptorId found = check(reg->typeIndex().candidatesFor(uid, name));
        if (found == detail::kInvalidDescriptorId && name != detail::kUnnamed)
            found = check(reg->typeIndex().candidatesFor(uid, detail::kUnnamed));
        return found;
    }

    template <class T>
    template <class U>
    bool Bean<T>::compatible() const noexcept {
        detail::Registry* reg = registry();
        if (reg == nullptr)
            return false;
        const detail::TypeId uid = reg->typeIdFor<U>();
        if (uid == detail::kInvalidTypeId)
            return false;
        if (object_ != nullptr) {
            const detail::Descriptor &d =
                reg->descriptorTable().at(bits_.f1.descId);
            const detail::DescriptorCold &cold =
                reg->descriptorTable().coldAt(bits_.f1.descId);
            if (cold.exposedType == uid)
                return true;
            if (cold.concreteType == uid)
                return true;
            // Check if U is another exposed base of the same primary.
            if (d.primaryDescriptor != detail::kInvalidDescriptorId) {
                return findAliasByPrimary(reg, uid, cold.name, d.primaryDescriptor)
                    != detail::kInvalidDescriptorId;
            }
            return false;
        }
        return reg->descriptorTable().coldAt(bits_.f2.descId).exposedType == uid;
    }

    template <class T>
    template <class U>
    Bean<U> Bean<T>::cast() const {
        if (!compatible<U>()) {
            throw ctr::ResolutionError(
                "Bean::cast: the bean is not compatible with the requested type."
                );
        }
        if (object_ == nullptr) {
            // Form 2: reinterpret bits (scope-proxy — no pointer adjustment needed).
            Bean<U> result;
            result.object_ = nullptr;
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            return result;
        }

        detail::Registry* reg = registry();
        const detail::Descriptor &selfDesc =
            reg->descriptorTable().at(bits_.f1.descId);
        const detail::DescriptorCold &selfCold =
            reg->descriptorTable().coldAt(bits_.f1.descId);
        const detail::TypeId uid = reg->typeIdFor<U>();
        const detail::DescriptorId selfPrimary = selfDesc.primaryDescriptor;

        // Case 1: U is the exact same exposed type — no adjustment needed.
        if (selfCold.exposedType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(object_);
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
                result.retainIfPrototype();
            return result;
        }

        // Recover the concrete pointer via adjustToConcrete.
        void *concretePtr = object_;
        if (selfCold.adjustToConcrete != nullptr) {
            concretePtr = selfCold.adjustToConcrete(object_);
        } else if (selfDesc.primaryDescriptor != bits_.f1.descId
            && selfDesc.primaryDescriptor != detail::kInvalidDescriptorId) {
            // Virtual base alias: adjustToConcrete == nullptr means no downcast available.
            // compatible<U>() returning true here means U == exposedType (already handled above).
            throw ctr::ResolutionError(
                "Bean::cast: cannot cast from virtual base — downcast unavailable."
                );
        }
        // else: primary (identity), concretePtr == object_

        // Case 2: U is the concrete type — use primary descriptor.
        if (selfCold.concreteType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(concretePtr);
            result.bits_.f1.slot = bits_.f1.slot;
            result.bits_.f1.descId = selfPrimary;
            result.registry_ = registry_;
            if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
                result.retainIfPrototype();
            return result;
        }

        // Case 3: U is another exposed base — find its alias descriptor.
        const detail::DescriptorId targetId =
            findAliasByPrimary(reg, uid, selfCold.name, selfPrimary);
        if (targetId == detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "Bean::cast: the bean is not compatible with the requested type."
                );
        }
        const detail::Descriptor &targetDesc = reg->descriptorTable().at(targetId);
        void *targetPtr = concretePtr;
        if (targetDesc.adjustToExposed != nullptr)
            targetPtr = targetDesc.adjustToExposed(concretePtr);

        Bean<U> result;
        result.object_ = static_cast<U *>(targetPtr);
        result.bits_.f1.slot = bits_.f1.slot;
        result.bits_.f1.descId = targetId;
        result.registry_ = registry_;
        if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
            result.retainIfPrototype();
        return result;
    }

    template <class T>
    template <class U>
    std::optional<Bean<U>> Bean<T>::tryCast() const {
        if (!compatible<U>())
            return std::nullopt;
        try {
            return cast<U>();
        } catch (...) {
            return std::nullopt;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // AnyBean — context, exact, compatible, cast, tryCast
    // ─────────────────────────────────────────────────────────────────────────────

    inline detail::Registry* AnyBean::registry() const noexcept {
        if (bits_.f1.slot != detail::kInvalidSlotId && object_ != nullptr)
            return static_cast<detail::PrototypeStore*>(registry_)->registry;
        return static_cast<detail::Registry*>(registry_);
    }

    inline BeanContext &AnyBean::context() const noexcept {
        return *registry()->rootContext();
    }

    template <class U>
    bool AnyBean::exact() const noexcept {
        detail::Registry* reg = registry();
        if (reg == nullptr)
            return false;
        const detail::TypeId tid = reg->typeIdFor<U>();
        if (tid == detail::kInvalidTypeId)
            return false;
        const detail::DescriptorId descId =
            object_ != nullptr ? bits_.f1.descId : bits_.f2.descId;
        return reg->descriptorTable().coldAt(descId).concreteType == tid;
    }

    template <class U>
    bool AnyBean::compatible() const noexcept {
        detail::Registry* reg = registry();
        if (reg == nullptr)
            return false;
        const detail::TypeId uid = reg->typeIdFor<U>();
        if (uid == detail::kInvalidTypeId)
            return false;
        const detail::DescriptorId descId =
            object_ != nullptr ? bits_.f1.descId : bits_.f2.descId;
        const detail::Descriptor &d = reg->descriptorTable().at(descId);
        const detail::DescriptorCold &cold = reg->descriptorTable().coldAt(descId);
        if (object_ == nullptr)
            return cold.exposedType == uid;
        if (cold.exposedType == uid)
            return true;
        if (cold.concreteType == uid)
            return true;
        if (d.primaryDescriptor != detail::kInvalidDescriptorId) {
            return findAliasByPrimary(reg, uid, cold.name, d.primaryDescriptor)
                != detail::kInvalidDescriptorId;
        }
        return false;
    }

    template <class U>
    Bean<U> AnyBean::cast() const {
        if (!compatible<U>()) {
            throw ctr::ResolutionError(
                "AnyBean::cast: the bean is not compatible with the requested type."
                );
        }
        if (object_ == nullptr) {
            Bean<U> result;
            result.object_ = nullptr;
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            return result;
        }
        detail::Registry* reg = registry();
        const detail::Descriptor &selfDesc =
            reg->descriptorTable().at(bits_.f1.descId);
        const detail::DescriptorCold &selfCold =
            reg->descriptorTable().coldAt(bits_.f1.descId);
        const detail::TypeId uid = reg->typeIdFor<U>();
        const detail::DescriptorId selfPrimary = selfDesc.primaryDescriptor;

        if (selfCold.exposedType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(object_);
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
                result.retainIfPrototype();
            return result;
        }

        void *concretePtr = object_;
        if (selfCold.adjustToConcrete != nullptr) {
            concretePtr = selfCold.adjustToConcrete(object_);
        } else if (selfDesc.primaryDescriptor != bits_.f1.descId
            && selfDesc.primaryDescriptor != detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "AnyBean::cast: cannot cast from virtual base — downcast unavailable."
                );
        }

        if (selfCold.concreteType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(concretePtr);
            result.bits_.f1.slot = bits_.f1.slot;
            result.bits_.f1.descId = selfPrimary;
            result.registry_ = registry_;
            if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
                result.retainIfPrototype();
            return result;
        }

        const detail::DescriptorId targetId =
            findAliasByPrimary(reg, uid, selfCold.name, selfPrimary);
        if (targetId == detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "AnyBean::cast: the bean is not compatible with the requested type."
                );
        }
        const detail::Descriptor &targetDesc = reg->descriptorTable().at(targetId);
        void *targetPtr = concretePtr;
        if (targetDesc.adjustToExposed != nullptr)
            targetPtr = targetDesc.adjustToExposed(concretePtr);

        Bean<U> result;
        result.object_ = static_cast<U *>(targetPtr);
        result.bits_.f1.slot = bits_.f1.slot;
        result.bits_.f1.descId = targetId;
        result.registry_ = registry_;
        if (result.bits_.f1.slot != detail::kInvalidSlotId && result.object_ != nullptr)
            result.retainIfPrototype();
        return result;
    }

    template <class U>
    std::optional<Bean<U>> AnyBean::tryCast() const {
        if (!compatible<U>())
            return std::nullopt;
        try {
            return cast<U>();
        } catch (...) {
            return std::nullopt;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T>::threadLocalResolve_  (Form 3 — thread-local resolution)
    //
    // Called by operator-> when scopeNameId == kThreadLocalSentinel.
    // Never caches the pointer — resolves against the calling thread's TL store
    // on every invocation using the primary descriptor. If not yet materialized
    // on this thread, materializes and adjusts to f2.descId's exposed type.
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    T *Bean<T>::threadLocalResolve_() const noexcept {
        auto* reg = static_cast<detail::Registry*>(registry_);
        const detail::DescriptorId descId = bits_.f2.descId;
        const detail::Descriptor &desc = reg->descriptorTable().at(descId);
        const detail::DescriptorId primaryId =
            desc.primaryDescriptor != detail::kInvalidDescriptorId
                ? desc.primaryDescriptor
                : descId;

        void *instance = detail::tlData().findInstance(reg->registryId(), primaryId);
        if (instance != nullptr) {
            if (desc.adjustToExposed != nullptr)
                instance = desc.adjustToExposed(instance);
            return static_cast<T *>(instance);
        }

        detail::ResolutionContext ctx{*reg, nullptr};
        try {
            instance = reg->materializeThreadLocalInstance(descId, ctx);
        } catch (...) {
            return nullptr;
        }
        if (desc.adjustToExposed != nullptr)
            instance = desc.adjustToExposed(instance);
        return static_cast<T *>(instance);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T>::metadata() / AnyBean::metadata()
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    BeanMetadata Bean<T>::metadata() const noexcept {
        detail::Registry* reg = registry();
        if (object_ != nullptr && reg != nullptr) {
            const detail::Descriptor &d = reg->descriptorTable().at(bits_.f1.descId);
            const detail::DescriptorCold &cold = reg->descriptorTable().coldAt(bits_.f1.descId);
            return BeanMetadata{cold.observedTypeGetter, cold.exactTypeGetter,
                                cold.nameStr ? cold.nameStr : "", cold.factoryMethodName,
                                d.lifetime, cold.origin, cold.reflectiveData};
        }
        if (reg != nullptr) {
            const detail::Descriptor &d = reg->descriptorTable().at(bits_.f2.descId);
            const detail::DescriptorCold &cold = reg->descriptorTable().coldAt(bits_.f2.descId);
            return BeanMetadata{cold.observedTypeGetter, cold.exactTypeGetter,
                                cold.nameStr ? cold.nameStr : "", cold.factoryMethodName,
                                d.lifetime, cold.origin, cold.reflectiveData};
        }
        return BeanMetadata{nullptr, nullptr, "", detail::Lifetime::Singleton,
                            detail::Origin::AnnotatedType, nullptr};
    }

    inline BeanMetadata AnyBean::metadata() const noexcept {
        detail::Registry* reg = registry();
        if (reg != nullptr) {
            const detail::DescriptorId descId =
                object_ != nullptr ? bits_.f1.descId : bits_.f2.descId;
            const detail::Descriptor &d = reg->descriptorTable().at(descId);
            const detail::DescriptorCold &cold = reg->descriptorTable().coldAt(descId);
            return BeanMetadata{cold.observedTypeGetter, cold.exactTypeGetter,
                                cold.nameStr ? cold.nameStr : "", cold.factoryMethodName,
                                d.lifetime, cold.origin, cold.reflectiveData};
        }
        return BeanMetadata{nullptr, nullptr, "", detail::Lifetime::Singleton,
                            detail::Origin::AnnotatedType, nullptr};
    }

} // namespace ctr
