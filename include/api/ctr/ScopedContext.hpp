#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <typeindex>
#include <typeinfo>

#include "BeanContext.hpp"
#include "Errors.hpp"

#include "../../internal/NameId.hpp"
#include "../../internal/detail/DefaultsTable.hpp"
#include "../../internal/detail/SessionStore.hpp"

namespace ctr {

/**
 * @brief Context bound to a user scope key.
 *
 * Inherits standard resolution and lifecycle operations from BeanContext.
 * Adds its own `start`/`stop`/`restart` cycle that governs the session store.
 */
class ScopedContext : public BeanContext {
public:
    /**
     * @brief Starts this scope: sizes the session store, marks the scope started.
     *
     * Idempotent: calling after a successful `start()` is a no-op.
     * Does NOT call `Registry::start`; the root must already be started.
     * @throws ContextStateError if the root registry has not been started.
     */
    ScopedContext& start();

    /**
     * @brief Stops this scope: destroys all session instances in reverse construction
     * order, clears the session store, marks the scope stopped.
     *
     * Preserves `scopeNameId_`, `userData`, and tracked handles.
     * Idempotent: calling on an already-stopped scope is a no-op.
     */
    ScopedContext& stop();

    /**
     * @brief Restarts this scoped context: equivalent to `stop()` followed by `start()`.
     * @return Current scoped context for chaining.
     */
    ScopedContext& restart();

    /**
     * @brief Attaches mutable user data to this scope.
     * @tparam T User data type.
     * @param value User data reference.
     * @return Current scoped context for chaining.
     */
    template <class T>
    ScopedContext& userData(T& value);

    /**
     * @brief Clears user data from this scope.
     * @param value Must be nullptr.
     * @return Current scoped context for chaining.
     */
    ScopedContext& userData(std::nullptr_t value);

    /**
     * @brief Gets mutable user data when type-compatible.
     * @tparam T Requested user data type.
     * @return Optional mutable reference wrapper.
     */
    template <class T>
    std::optional<std::reference_wrapper<T>> userData();

    /**
     * @brief Gets const user data when type-compatible.
     * @tparam T Requested user data type.
     * @return Optional const reference wrapper.
     */
    template <class T>
    std::optional<std::reference_wrapper<const T>> userData() const;

    /**
     * @brief Resolves or creates a scoped context by stable key under the owning root.
     * @param key Stable user key.
     * @return Scoped child context.
     */
    ScopedContext& resolveScope(std::string_view key);

    /**
     * @brief Binds an externally constructed session instance to this scope.
     *
     * Before the scope's `start()`, registers a pending instance that enters
     * the lifecycle at `start()`.  On a stopped scope, accepted for the next
     * `start()`.  After `start()`, stores immediately in the session store.
     *
     * @tparam T Bound type.
     * @param object  Ownership of the session instance.
     * @param options Binding options (`name`, `priority`).
     * @return This scope for chaining.
     * @throws ctr::ConfigurationError if T is unknown after root `start()`.
     */
    template <class T>
    ScopedContext& bindSession(std::unique_ptr<T> object, BindOptions options = {});

    /** @brief Destroys pending session instances that were never started. */
    ~ScopedContext();

protected:
    /**
     * @brief Creates a scoped context sharing the root registry.
     * @param registry Shared registry from the owning root context.
     * @param root Non-owning pointer to the root context that owns this scope.
     */
    explicit ScopedContext(std::shared_ptr<ctr::detail::Registry> registry, BeanContext* root);

    /**
     * @brief Scoped contexts never accept discovery contributions.
     * @throws ContextStateError Always thrown by this public contract.
     */
    void assertCanDiscover_() const override {
        throw ContextStateError("discover<...>() is not supported on ScopedContext.");
    }

private:
    friend class BeanContext;
    template <class> friend class Bean;
    friend class detail::Registry;

    BeanContext* root_;

    detail::NameId   scopeNameId_ = detail::kInvalidNameId;
    detail::SessionStore sessionStore_;
    bool             scopeStarted_ = false;
    /// Scope-local default named qualifiers; consulted before root defaults.
    detail::DefaultsTable scopedDefaults_;
    /// Fast-path flag: false until this scope configures at least one local default.
    std::atomic<bool> hasAnyScopedDefault_{false};

    /// Non-owning userData pointer; null when no userData is attached.
    void*            userData_     = nullptr;
    /// Exact type of the stored userData; typeid(void) when null.
    std::type_index  userDataType_{typeid(void)};

    /// Pre-start (or stopped-scope) runtime session bindings pending the next start().
    struct PendingRuntimeSession {
        detail::DescriptorId descId;
        void*                instance;
    };
    std::vector<PendingRuntimeSession> pendingRuntimeSessions_;
};

} // namespace ctr
