#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Config.hpp"

#include "AnyBean.hpp"
#include "Errors.hpp"
#include "ListenerHandle.hpp"
#include "Markers.hpp"
#include "Options.hpp"

#include <functional>
#include <shared_mutex>
#include <typeindex>
#include "../../internal/detail/HashUtils.hpp"
#include "../../internal/NameId.hpp"

namespace CTORIUM_NAMESPACE {

class BeanContext;
class ScopedContext;

namespace detail {

/**
 * @brief Internal bridge used by discover<...>() to submit discovery contributions.
 *
 * The concrete contribution layout is intentionally hidden from the public API.
 */
template <auto... Roots>
inline void submitDiscoveryContribution(BeanContext& context, DiscoverOptions options);

} // namespace detail

/**
 * @brief Public entry point for discovery, configuration, lifecycle, and resolution.
 */
class BeanContext {
public:
    virtual ~BeanContext();

    /**
     * @brief Resolves the default process-wide context.
     * @return Stable public context instance.
     */
    static BeanContext& resolveContext();

    /**
     * @brief Resolves a context identified by a user-provided stable key.
     * @param key Stable user key.
     * @return Stable public context instance for the key.
     */
    static BeanContext& resolveContext(std::string_view key);

    /**
     * @brief Registers discovery contributions for compile-time roots on this root context.
     *
     * This operation is pre-start only. It does not materialize user beans and only records
     * contributions for later merge/deduplication/indexing at start time.
     *
     * Roots must be visible at the call-site instantiation point.
     *
     * @tparam Roots Compile-time discovery roots.
     * @param options Discovery behavior options.
     * @return Current context for chaining.
     * @throws ContextStateError If called after start, or on a scoped context.
     */
    template <auto... Roots>
    BeanContext& discover(DiscoverOptions options = {}) {
        detail::submitDiscoveryContribution<Roots...>(*this, options);
        return *this;
    }

    /**
     * @brief Starts this context.
     * @return Current context for chaining.
     */
    BeanContext& start();

    /**
     * @brief Stops this context and releases all resources (terminal operation).
     *
     * Destroys all beans in reverse construction order, releases scope and
     * singleton stores, and removes this context's entry from the global table.
     * Any reference to this context obtained via `resolveContext()` becomes
     * dangling after this call returns.  A subsequent `resolveContext(key)` will
     * create a new, unstarted context for the same key.
     *
     * After `stop()` returns, the caller must not access `*this` again.
     */
    void stop();

    /**
     * @brief Resolves a bean by type.
     * @tparam T Requested bean type.
     * @return Typed tracked handle.
     */
    template <class T>
    Bean<T> resolve();

    /**
     * @brief Resolves a bean by type and named qualifier.
     * @tparam T Requested bean type.
     * @param key Named selector.
     * @return Typed tracked handle.
     */
    template <class T>
    Bean<T> resolve(named key);

    /**
     * @brief Sets the default named qualifier for type T.
     * @tparam T Target type.
     * @param name Default name.
     * @return Current context for chaining.
     */
    template <class T>
    BeanContext& defaultNamed(std::string_view name);

    /**
     * @brief Clears the default named qualifier for type T.
     * @tparam T Target type.
     * @param name Must be nullptr.
     * @return Current context for chaining.
     */
    template <class T>
    BeanContext& defaultNamed(std::nullptr_t name);

    /**
     * @brief Binds an externally created singleton object.
     * @tparam T Bound type.
     * @param object Ownership of the singleton object.
     * @param options Binding options.
     * @return Current context for chaining.
     */
    template <class T>
    BeanContext& bindSingleton(std::unique_ptr<T> object, BindOptions options = {});

    /**
     * @brief Resolves or creates a scoped context by stable key.
     * @param key Stable user key.
     * @return Scoped child context.
     */
    ScopedContext& resolveScope(std::string_view key);

    /**
     * @brief Registers a typed listener for a lifecycle phase.
     * @tparam T Target bean type filter.
     * @tparam Callback Callback type.
     * @tparam PhaseTag Listener phase tag type.
     * @param phase Phase tag value.
     * @param callback Listener callback.
     * @param options Listener registration options.
     * @return Opaque handle that can be removed manually.
     *
     * A listener registered on a `ScopedContext` observes only beans emitted by
     * that exact scope. A listener registered on the root context observes root
     * beans and beans from every scope.
     */
    template <class T, class Callback, class PhaseTag>
    ListenerHandle on(PhaseTag phase, Callback&& callback, ListenerOptions options = {});

    /**
     * @brief Registers a global listener for a lifecycle phase.
     *
     * Global callbacks receive `const AnyBean&`.
     *
     * @tparam Callback Callback type.
     * @tparam PhaseTag Listener phase tag type.
     * @param phase Phase tag value.
     * @param callback Listener callback.
     * @param options Listener registration options.
     * @return Opaque handle that can be removed manually.
     *
     * A listener registered on a `ScopedContext` observes only beans emitted by
     * that exact scope. A listener registered on the root context observes root
     * beans and beans from every scope.
     */
    template <class Callback, class PhaseTag>
    ListenerHandle on(PhaseTag phase, Callback&& callback, ListenerOptions options = {});

    /**
     * @brief Removes a listener by handle.
     * @param handle Listener handle.
     */
    void remove(const ListenerHandle& handle);

protected:
    /**
     * @brief Creates a root context with the given registry key.
     */
    explicit BeanContext(std::string key);

    /**
     * @brief Creates a context sharing an existing registry (used by ScopedContext).
     */
    explicit BeanContext(std::shared_ptr<CTORIUM_NAMESPACE::detail::Registry> registry);

    /**
     * @brief Internal precondition guard for discover<...>().
     *
     * Throws ContextStateError when discovery is not allowed.
     * Defined in ContextLifecycleImpl.hpp.
     */
    virtual void assertCanDiscover_() const;

    [[nodiscard]] CTORIUM_NAMESPACE::detail::Registry& core() noexcept { return *registry_; }

    std::shared_ptr<CTORIUM_NAMESPACE::detail::Registry> registry_;
    /** Non-null only for ScopedContext instances; set in ScopedContext constructor body. */
    ScopedContext* asScope_ = nullptr;

private:
    template <auto... Roots>
    friend void detail::submitDiscoveryContribution(BeanContext& context, DiscoverOptions options);

    std::string key_;
    std::unordered_map<std::string, std::unique_ptr<ScopedContext>,
                       CTORIUM_NAMESPACE::detail::StringViewHash, std::equal_to<>> scopes_;
    std::shared_mutex scopesMutex_;

    struct DeferredListener {
        std::type_index                  typeIndex;
        std::size_t                      phaseIndex;
        detail::NameId                   listenerScope;
        std::function<void(const void*)> callback;
        int                              priority;
        std::size_t                      token;
    };
    std::vector<DeferredListener> deferredListeners_;
    void flushDeferredListeners_();
};

} // namespace CTORIUM_NAMESPACE
