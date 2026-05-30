#pragma once

#ifndef CTORIUM_DYNAMIC_LINK

#include <algorithm>
#include <cassert>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "Registry.hpp"
#include "BeanDescriptorGen.hpp"
#include "ResolutionContext.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/AnyBean.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/BeanMetadata.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// phaseIndexFor<PhaseTag>  (declared before namespace ctr to allow Phase-1
// lookup from BeanContext::on<T> template bodies defined below)
// ─────────────────────────────────────────────────────────────────────────────

namespace ctr::detail {

    template <class PhaseTag>
    [[nodiscard]] std::size_t phaseIndexFor(PhaseTag) {
        if constexpr (std::is_same_v<PhaseTag, ctr::onInitialized_t>)
            return ListenerStore::phaseInitialized();
        else if constexpr (std::is_same_v<PhaseTag, ctr::onCreated_t>)
            return ListenerStore::phaseCreated();
        else if constexpr (std::is_same_v<PhaseTag, ctr::onPreDestroy_t>)
            return ListenerStore::phasePreDestroy();
        else if constexpr (std::is_same_v<PhaseTag, ctr::onDestroyed_t>)
            return ListenerStore::phaseDestroyed();
        else
            throw ctr::ConfigurationError(
                "BeanContext::on: unsupported phase tag; use ctr::onInitialized, "
                "ctr::onCreated, ctr::onPreDestroy, or ctr::onDestroyed."
                );
    }

} // namespace ctr::detail

namespace ctr {

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T> prototype refcount helpers
    //
    // Singletons carry kInvalidSlotId — no refcount management needed.
    // Prototype refcount operations are deferred to the prototype spec.
    // ─────────────────────────────────────────────────────────────────────────────

    // GCC 16.1 partial-inlining contract: see docs/gcc.md §10.  Keep these
    // helpers out-of-line and unattributed so the empty/singleton fast-exit folds
    // into hot callers while the prototype atomic tail stays in .text.unlikely.
    //
    // WARNING: do not add always_inline to force the whole helper into callers.
    // If an inline attribute is ever required, put it on this definition, not on
    // only the in-class template declaration; GCC 16.1 does not honor declaration-
    // only attributes for this template-member case.
    template <class T>
    void Bean<T>::retainIfPrototype() noexcept {
        if (object_ == nullptr)
            return; // Form 2 or empty
        if (bits_.f1.slot == detail::kInvalidSlotId)
            return; // singleton
        if (!registry_->startedRelaxed())
            return; // context stopped
        registry_->prototypeStore().retain(bits_.f1.slot);
    }

    template <class T>
    void Bean<T>::releaseIfPrototype() noexcept {
        if (object_ == nullptr)
            return; // Form 2 or empty
        if (bits_.f1.slot == detail::kInvalidSlotId)
            return; // singleton
        if (!registry_->startedRelaxed())
            return; // context stopped; stop() already ran
        if (!registry_->prototypeStore().releaseAcquire(bits_.f1.slot))
            return;
        // Single metas_[slot] access instead of separate pointerAt + descriptorIdAt.
        const auto [mem, descId] =
            registry_->prototypeStore().slotMetaAt(bits_.f1.slot);
        registry_->executeDestructionLifecycle(descId, mem);
        registry_->prototypeStore().reclaimSlot(bits_.f1.slot);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // AnyBean prototype refcount helpers
    // ─────────────────────────────────────────────────────────────────────────────

    inline void ctr::AnyBean::retainIfPrototype() noexcept {
        if (object_ == nullptr)
            return; // Form 2 or empty
        if (bits_.f1.slot == detail::kInvalidSlotId)
            return; // singleton
        if (!registry_->startedRelaxed())
            return; // context stopped
        registry_->prototypeStore().retain(bits_.f1.slot);
    }

    inline void ctr::AnyBean::releaseIfPrototype() noexcept {
        if (object_ == nullptr)
            return; // Form 2 or empty
        if (bits_.f1.slot == detail::kInvalidSlotId)
            return; // singleton
        if (!registry_->startedRelaxed())
            return; // context stopped
        if (!registry_->prototypeStore().releaseAcquire(bits_.f1.slot))
            return;
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
        detail::Registry &reg = core();
        detail::ResolutionContext ctx{reg, asScope_};
        return reg.resolve<T>(detail::kUnnamed, ctx);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::resolve<T>(named)  — named resolution
    //
    // Uses lookup() (not intern()) — resolution must not mutate the name table.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    Bean<T> BeanContext::resolve(named key) {
        detail::Registry &reg = core();
        const detail::NameId nameId = reg.nameInterning().lookup(
            key.name ? std::string_view{key.name} : std::string_view{}
            );
        if (nameId == detail::kInvalidNameId) {
            throw ResolutionError(
                std::string("BeanContext::resolve: unknown named qualifier '")
                + std::string(key.name ? key.name : "") + "'."
                );
        }
        detail::ResolutionContext ctx{reg, asScope_};
        return reg.resolve<T>(nameId, ctx);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::defaultNamed<T>(std::string_view)
    //
    // Interns the name and sets the runtime default NameId for T on this context.
    // Throws ContextStateError if called before start() (TypeId not yet assigned).
    // Throws ConfigurationError if T was never registered in this context.
    // Empty string clears the default (equivalent to nullptr).
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    BeanContext &BeanContext::defaultNamed(std::string_view name) {
        detail::Registry &reg = core();
        if (!reg.started()) {
            throw ContextStateError(
                "BeanContext::defaultNamed: context must be started; "
                "TypeId is not assigned before start()."
                );
        }
        const detail::TypeId typeId = reg.typeIdFor<T>();
        if (typeId == detail::kInvalidTypeId) {
            throw ConfigurationError(
                "BeanContext::defaultNamed: type T is not registered in this context."
                );
        }
        if (name.empty()) {
            reg.setDefault(typeId, detail::kUnnamed);
            return *this;
        }
        const detail::NameId nameId = reg.internNameSafe(name);
        reg.setDefault(typeId, nameId);
        return *this;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::defaultNamed<T>(std::nullptr_t)
    //
    // Clears the runtime default for T, restoring unnamed-key resolution.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    BeanContext &BeanContext::defaultNamed(std::nullptr_t) {
        detail::Registry &reg = core();
        if (!reg.started()) {
            throw ContextStateError(
                "BeanContext::defaultNamed: context must be started; "
                "TypeId is not assigned before start()."
                );
        }
        const detail::TypeId typeId = reg.typeIdFor<T>();
        if (typeId == detail::kInvalidTypeId)
            return *this; // T not registered — no-op
        reg.setDefault(typeId, detail::kUnnamed);
        return *this;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::on<T>(phase, callback, options)  — typed listener
    //
    // Wraps the typed callback in a void(const void*) closure that receives the
    // AnyBean*, builds a non-tracking Bean<T> view (kInvalidSlotId prevents
    // releaseIfPrototype on the temporary), and invokes the callback.
    //
    // The TypeId of T is resolved at registration time via typeIdFor<T>().
    // If the context is not yet started (typeIdFor<T>() == kInvalidTypeId), the
    // listener is stored with kInvalidTypeId and behaves as a global listener —
    // a known deviation from specs-api §14.2; note in deviation report.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T, class Callback, class PhaseTag>
    ListenerHandle BeanContext::on(
        PhaseTag phase, Callback &&callback,
        ListenerOptions options
        ) {
        static_assert(
            std::is_invocable_v<std::decay_t<Callback>, const Bean<T> &>,
            "BeanContext::on<T>: callback must accept (const ctr::Bean<T>&)."
            );

        detail::Registry &reg = core();
        const std::size_t phaseIdx = detail::phaseIndexFor(phase);

        auto wrapper =
            [cb = std::forward<Callback>(callback)](const void *vBean) {
            const ctr::AnyBean &anyBean =
                *static_cast<const ctr::AnyBean *>(vBean);
            // Non-tracking view: kInvalidSlotId prevents releaseIfPrototype
            // on the temporary, avoiding a double-release for prototype beans.
            // Prototype tracking in listener callbacks is deferred (AnyBean
            // tracking TODO).
            ctr::Bean<T> view = ctr::Bean<T>::makeDirect(
                static_cast<T *>(anyBean.object_),
                detail::kInvalidSlotId,
                anyBean.bits_.f1.descId,
                anyBean.registry_
                );
            cb(static_cast<const ctr::Bean<T> &>(view));
        };

        if (!reg.started()) {
            deferredListeners_.push_back(
                {
                    std::type_index(typeid(T)),
                    phaseIdx,
                    std::move(wrapper),
                    options.priority
                }
                );
            return ListenerHandle{};
        }

        const detail::TypeId typeId = reg.typeIdFor<T>();
        return reg.listenerStore().addListener(
            phaseIdx,
            typeId,
            std::move(wrapper),
            options.priority
            );
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::on(phase, callback, options)  — global listener
    //
    // Observes all beans on this context.  Callback receives const AnyBean&.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class Callback, class PhaseTag>
    ListenerHandle BeanContext::on(
        PhaseTag phase, Callback &&callback,
        ListenerOptions options
        ) {
        static_assert(
            std::is_invocable_v<std::decay_t<Callback>, const AnyBean &>,
            "BeanContext::on: global callback must accept (const ctr::AnyBean&)."
            );

        detail::Registry &reg = core();
        const std::size_t phaseIdx = detail::phaseIndexFor(phase);

        auto wrapper =
            [cb = std::forward<Callback>(callback)](const void *vBean) {
            const ctr::AnyBean &anyBean =
                *static_cast<const ctr::AnyBean *>(vBean);
            cb(anyBean);
        };

        return reg.listenerStore().addListener(
            phaseIdx,
            detail::kInvalidTypeId,
            std::move(wrapper),
            options.priority
            );
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // BeanContext::remove(handle)
    //
    // Delegates listener removal to the handle.  Idempotent (handle.remove() is
    // a no-op when already removed).
    // ─────────────────────────────────────────────────────────────────────────────

    inline void BeanContext::remove(const ListenerHandle &handle) {
        const_cast<ListenerHandle &>(handle).remove();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T> — context, exact, compatible, cast, tryCast
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    BeanContext &Bean<T>::context() const noexcept {
        if (object_ != nullptr) {
            // Form 1: singleton/prototype — owned by root.
            return *registry_->rootContext();
        }
        // Form 2: session handle — return the owning ScopedContext.
        ctr::ScopedContext *scope = registry_->findScope(bits_.f2.scopeNameId);
        if (scope != nullptr)
            return *scope;
        return *registry_->rootContext(); // fallback (scope no longer registered)
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T>::proxyResolve_()  — Form 2 proxy path (specs-internal §2.3)
    //
    // Algorithm:
    //   1. Look up scope by f2.scopeNameId.
    //   2. Scope absent or stopped → nullptr (no exception, specs-api §7.1).
    //   3. Find or materialize T in scope's SessionStore.
    //   4. Return object pointer.
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    T *Bean<T>::proxyResolve_() const noexcept {
        ctr::ScopedContext *scope = registry_->findScope(bits_.f2.scopeNameId);
        if (scope == nullptr || !scope->scopeStarted_)
            return nullptr;

        // TypeId lookup (cached per type).
        const detail::TypeId typeId = registry_->typeIdFor<T>();
        if (typeId == detail::kInvalidTypeId)
            return nullptr;

        // Find the descriptor for (typeId, candidateNameId).
        const std::vector<detail::DescriptorId> *candidates =
            registry_->typeIndex().candidatesFor(typeId, bits_.f2.candidateNameId);
        if (!candidates || candidates->empty())
            return nullptr;

        const detail::DescriptorId descId = (*candidates)[0];

        // Fast path: already materialized.
        void *instance = scope->sessionStore_.find(descId);
        if (instance != nullptr)
            return static_cast<T *>(instance);

        // Slow path: materialize (same two-phase pattern as singleton).
        detail::ResolutionContext ctx{*registry_, scope};
        try {
            instance = registry_->materializeSessionInstance(descId, scope, ctx);
        } catch (...) {
            return nullptr; // proxy path must not throw (specs-api §7.1)
        }
        return static_cast<T *>(instance);
    }

    template <class T>
    template <class U>
    bool Bean<T>::exact() const noexcept {
        if (registry_ == nullptr)
            return false;
        const detail::TypeId tid = registry_->typeIdFor<U>();
        if (tid == detail::kInvalidTypeId)
            return false;
        if (object_ != nullptr) {
            return registry_->descriptorTable().at(bits_.f1.descId).concreteType == tid;
        }
        // Form 2: look up descriptor via type index.
        const detail::TypeId ownTypeId = registry_->typeIdFor<T>();
        if (ownTypeId == detail::kInvalidTypeId)
            return false;
        const auto *candidates = registry_->typeIndex().candidatesFor(ownTypeId, bits_.f2.candidateNameId);
        if (!candidates || candidates->empty())
            return false;
        return registry_->descriptorTable().at((*candidates)[0]).concreteType == tid;
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
        if (registry_ == nullptr)
            return false;
        const detail::TypeId uid = registry_->typeIdFor<U>();
        if (uid == detail::kInvalidTypeId)
            return false;
        if (object_ != nullptr) {
            const detail::Descriptor &d =
                registry_->descriptorTable().at(bits_.f1.descId);
            if (d.exposedType == uid)
                return true;
            if (d.concreteType == uid)
                return true;
            // Check if U is another exposed base of the same primary.
            if (d.primaryDescriptor != detail::kInvalidDescriptorId) {
                return findAliasByPrimary(registry_, uid, d.name, d.primaryDescriptor)
                    != detail::kInvalidDescriptorId;
            }
            return false;
        }
        // Form 2: look up descriptor via type index.
        const detail::TypeId ownTypeId = registry_->typeIdFor<T>();
        if (ownTypeId == detail::kInvalidTypeId)
            return false;
        const auto *candidates = registry_->typeIndex().candidatesFor(ownTypeId, bits_.f2.candidateNameId);
        if (!candidates || candidates->empty())
            return false;
        return registry_->descriptorTable().at((*candidates)[0]).exposedType == uid;
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

        const detail::Descriptor &selfDesc =
            registry_->descriptorTable().at(bits_.f1.descId);
        const detail::TypeId uid = registry_->typeIdFor<U>();
        const detail::DescriptorId selfPrimary = selfDesc.primaryDescriptor;

        // Case 1: U is the exact same exposed type — no adjustment needed.
        if (selfDesc.exposedType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(object_);
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            result.retainIfPrototype();
            return result;
        }

        // Recover the concrete pointer via adjustToConcrete.
        void *concretePtr = object_;
        if (selfDesc.adjustToConcrete != nullptr) {
            concretePtr = selfDesc.adjustToConcrete(object_);
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
        if (selfDesc.concreteType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(concretePtr);
            result.bits_.f1.slot = bits_.f1.slot;
            result.bits_.f1.descId = selfPrimary;
            result.registry_ = registry_;
            result.retainIfPrototype();
            return result;
        }

        // Case 3: U is another exposed base — find its alias descriptor.
        const detail::DescriptorId targetId =
            findAliasByPrimary(registry_, uid, selfDesc.name, selfPrimary);
        if (targetId == detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "Bean::cast: the bean is not compatible with the requested type."
                );
        }
        const detail::Descriptor &targetDesc = registry_->descriptorTable().at(targetId);
        void *targetPtr = concretePtr;
        if (targetDesc.adjustToExposed != nullptr)
            targetPtr = targetDesc.adjustToExposed(concretePtr);

        Bean<U> result;
        result.object_ = static_cast<U *>(targetPtr);
        result.bits_.f1.slot = bits_.f1.slot;
        result.bits_.f1.descId = targetId;
        result.registry_ = registry_;
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

    inline BeanContext &AnyBean::context() const noexcept {
        return *registry_->rootContext();
    }

    template <class U>
    bool AnyBean::exact() const noexcept {
        if (object_ == nullptr || registry_ == nullptr)
            return false;
        const detail::TypeId tid = registry_->typeIdFor<U>();
        if (tid == detail::kInvalidTypeId)
            return false;
        return registry_->descriptorTable().at(bits_.f1.descId).concreteType == tid;
    }

    template <class U>
    bool AnyBean::compatible() const noexcept {
        if (object_ == nullptr || registry_ == nullptr)
            return false;
        const detail::TypeId uid = registry_->typeIdFor<U>();
        if (uid == detail::kInvalidTypeId)
            return false;
        const detail::Descriptor &d = registry_->descriptorTable().at(bits_.f1.descId);
        if (d.exposedType == uid)
            return true;
        if (d.concreteType == uid)
            return true;
        if (d.primaryDescriptor != detail::kInvalidDescriptorId) {
            return findAliasByPrimary(registry_, uid, d.name, d.primaryDescriptor)
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
        const detail::Descriptor &selfDesc =
            registry_->descriptorTable().at(bits_.f1.descId);
        const detail::TypeId uid = registry_->typeIdFor<U>();
        const detail::DescriptorId selfPrimary = selfDesc.primaryDescriptor;

        if (selfDesc.exposedType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(object_);
            result.bits_ = std::bit_cast<typename Bean<U>::Bits>(bits_);
            result.registry_ = registry_;
            result.retainIfPrototype();
            return result;
        }

        void *concretePtr = object_;
        if (selfDesc.adjustToConcrete != nullptr) {
            concretePtr = selfDesc.adjustToConcrete(object_);
        } else if (selfDesc.primaryDescriptor != bits_.f1.descId
            && selfDesc.primaryDescriptor != detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "AnyBean::cast: cannot cast from virtual base — downcast unavailable."
                );
        }

        if (selfDesc.concreteType == uid) {
            Bean<U> result;
            result.object_ = static_cast<U *>(concretePtr);
            result.bits_.f1.slot = bits_.f1.slot;
            result.bits_.f1.descId = selfPrimary;
            result.registry_ = registry_;
            result.retainIfPrototype();
            return result;
        }

        const detail::DescriptorId targetId =
            findAliasByPrimary(registry_, uid, selfDesc.name, selfPrimary);
        if (targetId == detail::kInvalidDescriptorId) {
            throw ctr::ResolutionError(
                "AnyBean::cast: the bean is not compatible with the requested type."
                );
        }
        const detail::Descriptor &targetDesc = registry_->descriptorTable().at(targetId);
        void *targetPtr = concretePtr;
        if (targetDesc.adjustToExposed != nullptr)
            targetPtr = targetDesc.adjustToExposed(concretePtr);

        Bean<U> result;
        result.object_ = static_cast<U *>(targetPtr);
        result.bits_.f1.slot = bits_.f1.slot;
        result.bits_.f1.descId = targetId;
        result.registry_ = registry_;
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
    // Bean<T>::threadLocalResolve_  (Form 3 — specs-internal §15.3 B1)
    //
    // Called by operator-> when scopeNameId == kThreadLocalSentinel.
    // Never caches the pointer — resolves against the calling thread's TL store
    // on every invocation.  If not yet materialized on this thread, materializes.
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    T *Bean<T>::threadLocalResolve_() const noexcept {
        const detail::TypeId typeId = registry_->typeIdFor<T>();
        if (typeId == detail::kInvalidTypeId)
            return nullptr;

        const std::vector<detail::DescriptorId> *candidates =
            registry_->typeIndex().candidatesFor(typeId, bits_.f2.candidateNameId);
        if (!candidates || candidates->empty())
            return nullptr;

        const detail::DescriptorId descId = (*candidates)[0];

        // Fast path: already materialized on this thread.
        void *instance = detail::tlData().findInstance(registry_->registryId(), descId);
        if (instance != nullptr)
            return static_cast<T *>(instance);

        // Slow path: materialize.
        detail::ResolutionContext ctx{*registry_, nullptr};
        try {
            instance = registry_->materializeThreadLocalInstance(descId, ctx);
        } catch (...) {
            return nullptr;
        }
        return static_cast<T *>(instance);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Bean<T>::metadata() / AnyBean::metadata()
    // ─────────────────────────────────────────────────────────────────────────────

    template <class T>
    BeanMetadata Bean<T>::metadata() const noexcept {
        if (object_ != nullptr && registry_ != nullptr) {
            // Form 1: direct handle — descriptor is stored in bits_.f1.descId.
            const detail::Descriptor &d = registry_->descriptorTable().at(bits_.f1.descId);
            return BeanMetadata{d.observedTypeGetter, d.exactTypeGetter,
                                d.nameStr ? d.nameStr : "", d.lifetime, d.origin, d.reflectiveData};
        }
        if (registry_ != nullptr) {
            // Form 2 (session proxy) or Form 3 (thread-local): recover descriptor from type index.
            // Mirror the exact() Form-2 branch to avoid touching the wrong union member.
            const detail::TypeId ownTypeId = registry_->typeIdFor<T>();
            if (ownTypeId != detail::kInvalidTypeId) {
                const auto *candidates =
                    registry_->typeIndex().candidatesFor(ownTypeId, bits_.f2.candidateNameId);
                if (candidates && !candidates->empty()) {
                    const detail::Descriptor &d =
                        registry_->descriptorTable().at((*candidates)[0]);
                    return BeanMetadata{d.observedTypeGetter, d.exactTypeGetter,
                                        d.nameStr ? d.nameStr : "", d.lifetime, d.origin, d.reflectiveData};
                }
            }
        }
        return BeanMetadata{nullptr, nullptr, "", detail::Lifetime::Singleton,
                            detail::Origin::AnnotatedType, nullptr};
    }

    inline BeanMetadata AnyBean::metadata() const noexcept {
        if (object_ != nullptr && registry_ != nullptr) {
            const detail::Descriptor &d = registry_->descriptorTable().at(bits_.f1.descId);
            return BeanMetadata{d.observedTypeGetter, d.exactTypeGetter,
                                d.nameStr ? d.nameStr : "", d.lifetime, d.origin, d.reflectiveData};
        }
        return BeanMetadata{nullptr, nullptr, "", detail::Lifetime::Singleton,
                            detail::Origin::AnnotatedType, nullptr};
    }

} // namespace ctr

namespace ctr::detail {

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::executeDestructionLifecycle
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::executeDestructionLifecycle(DescriptorId descId, void *mem) noexcept {
        const Descriptor &d = descriptors_.at(descId);
        ResolutionContext ctx{*this};

        ctr::AnyBean anyBean;
        anyBean.object_ = mem;
        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
        anyBean.bits_.f1.descId = descId;
        anyBean.registry_ = this;

        listeners_.dispatch(ListenerStore::phasePreDestroy(), d.exposedType, &anyBean);

        if (d.preDestroy) {
            d.preDestroy(mem, static_cast<void *>(&ctx));
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

    inline void Registry::registerContextBean(BeanContext *ctx) {
        const TypeId typeId = typeInterning_.internByName(
            "ctr::BeanContext",
            TypeInfoGetter<ctr::BeanContext>::get
            );

        Descriptor d;
        d.exposedType = typeId;
        d.concreteType = typeId;
        d.name = kUnnamed;
        d.priority = 0;
        d.lifetime = Lifetime::Singleton;
        d.origin = Origin::RuntimeBinding;
        d.construct = [](void *, void *) noexcept {
        };
        d.destroy = [](void *) noexcept {
        };
        d.postConstruct = nullptr;
        d.preDestroy = nullptr;
        d.size = 0; // externally owned — executeDestructionLifecycle skips ::operator delete
        d.align = alignof(ctr::BeanContext);
        d.factoryMethodDescriptor = kInvalidDescriptorId;

        d.observedTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        d.exactTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        d.nameStr = "ctr::BeanContext";

        beanContextDescId_ = descriptors_.append(std::move(d));
        typeIndex_.insertCandidate(typeId, kUnnamed, beanContextDescId_);
        typeIndex_.updateSingleUnnamed(typeId);

        singletons_.resize(descriptors_.size()); // extend one slot for BeanContext
        singletons_.store(beanContextDescId_, static_cast<void *>(ctx));

        root_ = ctx;
        // Lifecycle dispatch (phaseInitialized / phaseCreated) is performed by start()
        // after releasing writeLock_ (A5: prevent deadlock from listener callbacks).
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::resolve<T>  — hot-path resolution (specs-internal §9)
    //
    // Lifetime coverage: Singleton, Prototype, Session, and ThreadLocal.
    // Per-lifetime materialization is delegated to materializeOne<T>().
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
        const NameId effectiveNameId =
            (nameId == kUnnamed && hasAnyDefault) ? defaults_.getDefault(typeId) : nameId;
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
                // precomputed named-head map only covers candidates known at start().
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
        return materializeOne<T>(descId, ctx);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeOne<T>
    //
    // Per-descriptor materialization: singleton (two-phase lock), prototype
    // (CycleGuard + slot activation), lifecycle dispatch.  Extracted from
    // resolve<T>. Used directly by resolve<T> for single-candidate materialization.
    // priority arbitration.
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    auto Registry::materializeOne(
        DescriptorId descId, ResolutionContext &ctx
        )
        -> ctr::Bean<T> {
        const Lifetime lt = descriptors_.lifetimeOf(descId);
        void *singletonInstance = nullptr;
        if (lt == Lifetime::Singleton) {
            singletonInstance = singletons_.find(descId);
            if (singletonInstance != nullptr) {
                return ctr::Bean<T>::makeDirect(
                    static_cast<T *>(singletonInstance),
                    kInvalidSlotId,
                    descId,
                    this
                    );
            }
        }

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
                        // Slow path: trigger primary materialization (two-phase, non-template).
                        materializeEagerSingleton(primaryId, ctx);
                        concretePtr = singletons_.find(primaryId);
                    }
                    void *exposedPtr = concretePtr;
                    if (desc.adjustToExposed != nullptr)
                        exposedPtr = desc.adjustToExposed(concretePtr);
                    return ctr::Bean<T>::makeDirect(
                        static_cast<T *>(exposedPtr),
                        kInvalidSlotId,
                        descId,
                        this
                        );
                }

                if (lt == Lifetime::Prototype) {
                    const Descriptor &primaryDesc = descriptors_.at(primaryId);
                    CycleGuard guard(materializationStack(), primaryId);

                    void *mem;
                    SlotId slotId;
                    if (primaryDesc.allocAndConstruct != nullptr) {
                        mem = primaryDesc.allocAndConstruct(static_cast<void *>(&ctx));
                        slotId = prototypes_.allocate(primaryId, mem);
                    } else {
                        mem = ::operator new(primaryDesc.size, std::align_val_t{primaryDesc.align});
                        slotId = prototypes_.allocate(primaryId, mem);
                        try {
                            primaryDesc.construct(mem, static_cast<void *>(&ctx));
                        } catch (...) {
                            ::operator delete(
                                mem,
                                primaryDesc.size,
                                std::align_val_t{primaryDesc.align}
                                );
                            prototypes_.reclaim(slotId);
                            throw;
                        }
                    }
                    prototypes_.activate(slotId);

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
                        anyBean.registry_ = this;
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
                    return ctr::Bean<T>::makeDirect(
                        static_cast<T *>(exposedPtr),
                        slotId,
                        descId,
                        this
                        );
                }

                // Session/ThreadLocal aliases: redirect to primary for store keying.
                // The exposed pointer is computed after materialization.
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
                {
                    std::unique_lock lock(writeLock_);
                    for (DescriptorId existing : materializationStack()) {
                        if (existing == descId) {
                            throw ctr::ResolutionError(
                                "Registry::resolve: dependency cycle "
                                "detected during singleton materialization."
                                );
                        }
                    }
                    cv_.wait(
                        lock,
                        [&] {
                            return singletons_.find(descId) != nullptr
                                || std::find_if(
                                    materializing_.begin(),
                                    materializing_.end(),
                                    [descId](const auto &p) {
                                        return p.first == descId && p.second == nullptr;
                                    }
                                    ) == materializing_.end();
                        }
                        );
                    instance = singletons_.find(descId);
                    if (instance == nullptr) {
                        for (DescriptorId existing : materializationStack()) {
                            if (existing == descId) {
                                throw ctr::ResolutionError(
                                    "Registry::resolve: dependency cycle "
                                    "detected during singleton materialization."
                                    );
                            }
                        }
                        materializationStack().push_back(descId);
                        materializing_.push_back({descId, nullptr});
                        didMaterialize = true;
                    }
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
                                [descId](const auto &p) {
                                    return p.first == descId && p.second == nullptr;
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
                            [descId](const auto &p) {
                                return p.first == descId && p.second == nullptr;
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

            // Return Bean<T> Form 1 (kInvalidSlotId = singleton, no refcount)
            return ctr::Bean<T>::makeDirect(
                static_cast<T *>(instance),
                kInvalidSlotId,
                descId,
                this
                );
        }

        case Lifetime::Prototype: {
            CycleGuard guard(materializationStack(), descId);

            void *mem;
            SlotId slotId;

            if (desc.allocAndConstruct != nullptr) {
                mem = desc.allocAndConstruct(static_cast<void *>(&ctx));
                slotId = prototypes_.allocate(descId, mem);
            } else {
                mem = ::operator new(desc.size, std::align_val_t{desc.align});
                slotId = prototypes_.allocate(descId, mem);
                try {
                    desc.construct(mem, static_cast<void *>(&ctx));
                } catch (...) {
                    ::operator delete(mem, desc.size, std::align_val_t{desc.align});
                    prototypes_.reclaim(slotId);
                    throw;
                }
            }

            prototypes_.activate(slotId);

            const bool dispatchInitialized =
                listeners_.hasListeners(ListenerStore::phaseInitialized());
            const bool dispatchCreated =
                listeners_.hasListeners(ListenerStore::phaseCreated());
            if (dispatchInitialized || dispatchCreated) {
                ctr::AnyBean anyBean;
                anyBean.object_ = mem;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(slotId);
                anyBean.bits_.f1.descId = descId;
                anyBean.registry_ = this;
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

            return ctr::Bean<T>::makeDirect(
                static_cast<T *>(mem),
                slotId,
                descId,
                this
                );
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
            void *instance = materializeSessionInstance(descId, ctx.scope, ctx);
            const NameId candidateNameId = desc.name;
            return ctr::Bean<T>::makeProxy(ctx.scope->scopeNameId_, candidateNameId, this);
        }

        case Lifetime::ThreadLocal: {
            // Scope is transparent for threadLocal (specs-api §8): always resolve
            // as if from root — ctx.scope is ignored.
            void *instance = materializeThreadLocalInstance(descId, ctx);
            const NameId candidateNameId = desc.name;
            return ctr::Bean<T>::makeThreadLocal(candidateNameId, this);
        }
        }

        // Unreachable; suppress compiler warnings.
        throw ctr::ConfigurationError("Registry::resolve: unhandled lifetime.");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeSessionInstance
    //
    // Two-phase lazy materialization of a session bean into scope's SessionStore.
    // Uses the same writeLock_ / cv_ as the singleton path; uses separate
    // sessionMaterializing_ entries keyed by (descId, scope) so that the same
    // descId in different scopes materializes independently.
    // Dispatches onInitialized → postConstruct → onCreated on first materialization.
    // Returns the live pointer; throws ResolutionError on dependency cycle.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void *Registry::materializeSessionInstance(
        DescriptorId descId,
        ctr::ScopedContext *scope,
        ResolutionContext &ctx
        ) {
        // Fast path: already materialized (lock-free acquire).
        void *instance = scope->sessionStore_.find(descId);
        if (instance != nullptr)
            return instance;

        const Descriptor &desc = descriptors_.at(descId);
        bool didMaterialize = false;

        // Phase 1: claim (descId, scope) or wait for a peer on the same scope.
        {
            std::unique_lock lock(writeLock_);
            for (DescriptorId existing : materializationStack()) {
                if (existing == descId) {
                    throw ctr::ResolutionError(
                        "Registry::resolve: dependency cycle detected during "
                        "session bean materialization."
                        );
                }
            }
            cv_.wait(
                lock,
                [&] {
                    return scope->sessionStore_.find(descId) != nullptr
                        || std::find_if(
                            materializing_.begin(),
                            materializing_.end(),
                            [descId, scope](const auto &p) {
                                return p.first == descId && p.second == scope;
                            }
                            ) == materializing_.end();
                }
                );
            instance = scope->sessionStore_.find(descId);
            if (instance == nullptr) {
                // RuntimeBinding session beans must be pre-stored in sessionStore_ at
                // scope start(). A nil slot means the binding was not renewed for this cycle.
                if (desc.origin == Origin::RuntimeBinding) {
                    throw ctr::ContextStateError(
                        "Registry::materializeSessionInstance: bound session bean has no "
                        "instance for this scope cycle; call bindSession<T>() before start()."
                        );
                }
                for (DescriptorId existing : materializationStack()) {
                    if (existing == descId) {
                        throw ctr::ResolutionError(
                            "Registry::resolve: dependency cycle detected during "
                            "session bean materialization."
                            );
                    }
                }
                materializationStack().push_back(descId);
                materializing_.push_back({descId, scope});
                didMaterialize = true;
            }
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
                        [descId, scope](const auto &p) {
                            return p.first == descId && p.second == scope;
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
                scope->sessionStore_.store(descId, mem);
                auto it = std::find_if(
                    materializing_.begin(),
                    materializing_.end(),
                    [descId, scope](const auto &p) {
                        return p.first == descId && p.second == scope;
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
            anyBean.bits_.f1.descId = descId;
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
        const std::size_t count = prototypes_.slotCount();
        for (std::size_t s = count; s > 0; --s) {
            void *mem = prototypes_.memoryAt(static_cast<SlotId>(s - 1));
            if (mem != nullptr) {
                executeDestructionLifecycle(
                    prototypes_.descriptorIdAt(static_cast<SlotId>(s - 1)), mem);
            }
        }
        prototypes_.releaseAll();

        return true;
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
            void *mem = scope.sessionStore_.find(*it); // lock-free acquire
            if (mem != nullptr)
                executeDestructionLifecycle(*it, mem);
        }

        // Reacquire writeLock_ for the final store reset.
        std::lock_guard lock(writeLock_);
        scope.sessionStore_.releaseAll();
    }

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
    // Finds or creates the threadLocal instance for (this, descId) on the calling
    // thread.  No lock needed — the TL store is thread-private.  Registration with
    // the registry (for stop()-time cleanup) is done once per (thread, registry)
    // pair under tlMutex_.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void *Registry::materializeThreadLocalInstance(
        DescriptorId descId,
        ResolutionContext &ctx
        ) {
        TLData &tl = tlData();

        // Fast path: already materialized on this thread.
        void *instance = tl.findInstance(registryId(), descId);
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
        const Descriptor &desc = descriptors_.at(descId);
        CycleGuard guard(materializationStack(), descId);

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
            throw;
        }

        tl.storeInstance(registryId(), descId, mem);

        // Lifecycle: C++ construction → onInitialized → postConstruct → onCreated.
        ctr::AnyBean anyBean;
        anyBean.object_ = mem;
        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
        anyBean.bits_.f1.descId = descId;
        anyBean.registry_ = this;
        listeners_.dispatch(ListenerStore::phaseInitialized(), desc.exposedType, &anyBean);
        if (desc.postConstruct)
            desc.postConstruct(mem, static_cast<void *>(&ctx));
        listeners_.dispatch(ListenerStore::phaseCreated(), desc.exposedType, &anyBean);

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

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::dispatchBoundSingletonLifecycle
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::dispatchBoundSingletonLifecycle() {
        for (const auto &sb : startLifecyclePending_) {
            ctr::AnyBean anyBean;
            anyBean.object_ = sb.instance;
            anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
            anyBean.bits_.f1.descId = sb.descId;
            anyBean.registry_ = this;
            listeners_.dispatch(ListenerStore::phaseInitialized(), sb.exposedTypeId, &anyBean);
            listeners_.dispatch(ListenerStore::phaseCreated(), sb.exposedTypeId, &anyBean);
        }
        startLifecyclePending_.clear();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::bindSingleton<T>
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename T>
    auto Registry::bindSingleton(
        std::unique_ptr<T> object, NameId nameId, int32_t priority
        )
        -> ctr::Bean<T> {

        if constexpr (std::is_same_v<T, ctr::BeanContext>) {
            throw ctr::ConfigurationError(
                "Registry::bindSingleton: ctr::BeanContext is registered implicitly at "
                "start(); explicit bindSingleton<ctr::BeanContext>() is forbidden "
                "(specs-api §5.5)."
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
                // Check already instantiated (any name key).
                const NameTable *table = typeIndex_.tableFor(typeId);
                if (table) {
                    for (const auto &[nid, candidates] : table->entries) {
                        for (DescriptorId did : candidates) {
                            if (singletons_.find(did) != nullptr) {
                                throw ctr::ConfigurationError(
                                    std::string("Registry::bindSingleton: type '") + kTypeName
                                    + "' is already instantiated in this context."
                                    );
                            }
                        }
                    }
                }
                // Check concurrent materialization of the same type.
                for (const auto &[did, scope] : materializing_) {
                    if (scope == nullptr && descriptors_.at(did).exposedType == typeId) {
                        throw ctr::ConfigurationError(
                            std::string("Registry::bindSingleton: type '") + kTypeName
                            + "' is currently being materialized; binding conflicts with "
                            "concurrent materialization (specs-api §11.3)."
                            );
                    }
                }

                // All checks passed: take ownership.
                void *rawPtr = object.release();

                Descriptor d;
                d.exposedType = typeId;
                d.concreteType = typeId;
                d.name = nameId;
                d.priority = priority;
                d.lifetime = Lifetime::Singleton;
                d.origin = Origin::RuntimeBinding;
                d.construct = [](void *, void *) noexcept {
                };
                d.destroy = &destroyThunk<T>;
                d.postConstruct = nullptr;
                d.preDestroy = preDestroyFn;
                d.size = sizeof(T);
                d.align = alignof(T);
                d.allocAndConstruct = nullptr;
                d.dealloc = &deallocBoundObjectThunk<T>;
                d.factoryMethodDescriptor = kInvalidDescriptorId;
                d.observedTypeGetter = &TypeInfoGetter<T>::get;
                d.exactTypeGetter = &TypeInfoGetter<T>::get;
                d.nameStr = kTypeName;

                const DescriptorId descId = descriptors_.append(std::move(d));
                // Store instance before publishing to TypeIndex so materializeOne always
                // finds the pre-stored pointer on any path.
                singletons_.growAndStore(descId, rawPtr);
                typeIndex_.insertCandidate(typeId, nameId, descId);
                typeIndex_.updateSingleUnnamed(typeId);

                // A5: release writeLock_ before dispatching so listener callbacks can call
                // registry operations without deadlocking.
                lock.unlock();

                ctr::AnyBean anyBean;
                anyBean.object_ = rawPtr;
                anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
                anyBean.bits_.f1.descId = descId;
                anyBean.registry_ = this;
                listeners_.dispatch(ListenerStore::phaseInitialized(), typeId, &anyBean);
                listeners_.dispatch(ListenerStore::phaseCreated(), typeId, &anyBean);

                return ctr::Bean<T>::makeDirect(static_cast<T *>(rawPtr), kInvalidSlotId, descId, this);
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
            return ctr::Bean<T>{}; // empty handle — use resolve<T>() after start()

        } // end else (!BeanContext)
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::~Registry
    // ─────────────────────────────────────────────────────────────────────────────

    inline Registry::~Registry() noexcept {
        (void)stopRegistryOwnedBeans();

        // Destroy pending runtime singletons that were never started into the lifecycle.
        for (const auto &pb : pendingRuntimeSingletons_) {
            if (pb.instance) {
                pb.destroy(pb.instance);
                if (pb.dealloc) {
                    pb.dealloc(pb.instance);
                } else if (pb.size != 0) {
                    ::operator delete(pb.instance, pb.size, std::align_val_t{pb.align});
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeEagerSingleton
    //
    // Non-template singleton materialization for eager construction at start().
    // Replicates the Singleton two-phase lock pattern from materializeOne<T> but
    // returns void (the caller does not need a typed Bean<T> handle).
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::materializeEagerSingleton(DescriptorId descId, ResolutionContext &ctx) {
        void *instance = singletons_.find(descId);
        if (instance != nullptr)
            return; // already materialized (dependency chain)

        const Descriptor &desc = descriptors_.at(descId);
        bool didMaterialize = false;

        // Phase 1: claim descId or wait for a concurrent materializer.
        {
            std::unique_lock lock(writeLock_);
            for (DescriptorId existing : materializationStack()) {
                if (existing == descId) {
                    throw ctr::ResolutionError(
                        "Registry: dependency cycle detected during eager singleton materialization."
                        );
                }
            }
            cv_.wait(
                lock,
                [&] {
                    return singletons_.find(descId) != nullptr
                        || std::find_if(
                            materializing_.begin(),
                            materializing_.end(),
                            [descId](const auto &p) {
                                return p.first == descId && p.second == nullptr;
                            }
                            ) == materializing_.end();
                }
                );
            instance = singletons_.find(descId);
            if (instance == nullptr) {
                for (DescriptorId existing : materializationStack()) {
                    if (existing == descId) {
                        throw ctr::ResolutionError(
                            "Registry: dependency cycle detected during eager singleton materialization."
                            );
                    }
                }
                materializationStack().push_back(descId);
                materializing_.push_back({descId, nullptr});
                didMaterialize = true;
            }
        }

        // Phase 2: construct outside lock.
        if (didMaterialize) {
            assert(desc.origin != Origin::RuntimeBinding);
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
                        [descId](const auto &p) {
                            return p.first == descId && p.second == nullptr;
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

            // Phase 3: store under lock, notify waiters.
            {
                std::lock_guard reLock(writeLock_);
                singletons_.store(descId, mem);
                auto it = std::find_if(
                    materializing_.begin(),
                    materializing_.end(),
                    [descId](const auto &p) {
                        return p.first == descId && p.second == nullptr;
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

            // Lifecycle dispatch: C++ construction → onInitialized → postConstruct → onCreated.
            ctr::AnyBean anyBean;
            anyBean.object_ = instance;
            anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
            anyBean.bits_.f1.descId = descId;
            anyBean.registry_ = this;

            listeners_.dispatch(ListenerStore::phaseInitialized(), desc.exposedType, &anyBean);
            if (desc.postConstruct) {
                desc.postConstruct(instance, static_cast<void *>(&ctx));
            }
            listeners_.dispatch(ListenerStore::phaseCreated(), desc.exposedType, &anyBean);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeEagerSingletons
    //
    // Collects all Singleton, non-RuntimeBinding, lazy=false descriptors, sorts by
    // descending priority, and materializes each.  Exceptions propagate (pass-through
    // policy per specs-api §17).
    // NOTE: when polymorphic-exposure lands, add a guard to skip alias descriptors
    // (primaryDescriptor != self) to avoid double-triggering on exposed aliases.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::materializeEagerSingletons() {
        std::vector<std::pair<int32_t, DescriptorId>> eager;
        const std::size_t count = descriptors_.size();
        eager.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const DescriptorId id = static_cast<DescriptorId>(i);
            const Descriptor &d = descriptors_.at(id);
            if (d.lifetime == Lifetime::Singleton
                && d.origin != Origin::RuntimeBinding
                && !d.lazy
                && d.primaryDescriptor == id) { // skip aliases (primaryDescriptor != self)
                eager.push_back({d.priority, id});
            }
        }
        std::sort(
            eager.begin(),
            eager.end(),
            [](const auto &a, const auto &b) {
                return a.first > b.first;
            }
            );

        ResolutionContext ctx{*this};
        for (const auto &[priority, id] : eager) {
            materializeEagerSingleton(id, ctx);
        }
    }

} // namespace ctr::detail

#endif // CTORIUM_DYNAMIC_LINK
