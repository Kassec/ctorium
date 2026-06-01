#pragma once

#include <shared_mutex>
#include <type_traits>
#include <utility>

#include "Registry.hpp"
#include "ResolutionContext.hpp"
#include "HashUtils.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

// -------------------------------------------------------------------------
// phaseIndexFor<PhaseTag>  (declared before namespace ctr to allow Phase-1
// lookup from BeanContext::on<T> template bodies defined below)
// -------------------------------------------------------------------------

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
        std::vector<ScopedContext*> scopes;
        {
            std::shared_lock<std::shared_mutex> lock(scopesMutex_);
            scopes.reserve(scopes_.size());
            for (auto& [key, scope] : scopes_) {
                (void)key;
                scopes.push_back(scope.get());
            }
        }
        for (ScopedContext* scope : scopes) {
            scope->flushDeferredListeners_();
        }
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
            entry.phaseIndex,
            typeId,
            entry.listenerScope,
            std::move(entry.callback),
            entry.priority);
    }
    core().listenerStore().finalizeListeners();
    deferredListeners_.clear();
}

inline void BeanContext::stop() {
    std::vector<ScopedContext*> scopes;
    {
        std::shared_lock<std::shared_mutex> lock(scopesMutex_);
        scopes.reserve(scopes_.size());
        for (auto& [key, scope] : scopes_) {
            (void)key;
            scopes.push_back(scope.get());
        }
    }
    for (ScopedContext* scope : scopes) {
        scope->stop();
    }

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
        const detail::DescriptorCold& cold = core().descriptorTable().coldAt(psb.descId);
        cold.destroy(psb.instance);
        if (cold.dealloc) {
            cold.dealloc(psb.instance);
        } else if (cold.size != 0) {
            ::operator delete(psb.instance, cold.size, std::align_val_t{cold.align});
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
        const detail::DescriptorCold& cold = reg.descriptors_.coldAt(psb.descId);
        sessionStore_.store(d.sessionSlot, psb.descId, psb.instance);
        ctr::AnyBean anyBean;
        anyBean.object_         = psb.instance;
        anyBean.bits_.f1.slot   = static_cast<std::uint32_t>(detail::kInvalidSlotId);
        anyBean.bits_.f1.descId = psb.descId;
        anyBean.registry_       = &reg;
        reg.listeners_.dispatch(
            detail::ListenerStore::phaseInitialized(),
            cold.exposedType,
            &anyBean,
            scopeNameId_);
        reg.listeners_.dispatch(
            detail::ListenerStore::phaseCreated(),
            cold.exposedType,
            &anyBean,
            scopeNameId_);
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
        ctr::ScopedContext* scope = asScope_;
        if (name.empty()) {
            if (scope != nullptr) {
                if (scope->scopedDefaults_.size() != 0) {
                    scope->scopedDefaults_.setDefault(typeId, detail::kUnnamed);
                }
            } else {
                reg.setDefault(typeId, detail::kUnnamed);
            }
            return *this;
        }
        const detail::NameId nameId = reg.internNameSafe(name);
        if (scope != nullptr) {
            if (scope->scopedDefaults_.size() == 0) {
                scope->scopedDefaults_.resize(reg.defaultsTable().size());
            }
            scope->scopedDefaults_.setDefault(typeId, nameId);
            scope->hasAnyScopedDefault_.store(true, std::memory_order_relaxed);
        } else {
            reg.setDefault(typeId, nameId);
        }
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
        ctr::ScopedContext* scope = asScope_;
        if (scope != nullptr) {
            if (scope->scopedDefaults_.size() != 0) {
                scope->scopedDefaults_.setDefault(typeId, detail::kUnnamed);
            }
        } else {
            reg.setDefault(typeId, detail::kUnnamed);
        }
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
    // listener is stored with kInvalidTypeId and observes all types, not only T.
    // This is the deliberate consequence of typed-listener registration before
    // T's TypeId has been interned.
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
        const detail::NameId listenerScope =
            asScope_ != nullptr ? asScope_->scopeNameId_ : detail::ListenerStore::kAllScopes;

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
                anyBean.registry()
                );
            cb(static_cast<const ctr::Bean<T> &>(view));
        };

        if (!reg.started()) {
            deferredListeners_.push_back(
                {
                    std::type_index(typeid(T)),
                    phaseIdx,
                    listenerScope,
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
            listenerScope,
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
        const detail::NameId listenerScope =
            asScope_ != nullptr ? asScope_->scopeNameId_ : detail::ListenerStore::kAllScopes;

        auto wrapper =
            [cb = std::forward<Callback>(callback)](const void *vBean) {
            const ctr::AnyBean &anyBean =
                *static_cast<const ctr::AnyBean *>(vBean);
            cb(anyBean);
        };

        return reg.listenerStore().addListener(
            phaseIdx,
            detail::kInvalidTypeId,
            listenerScope,
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
// ScopedContext::userData / userData
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

} // namespace ctr
