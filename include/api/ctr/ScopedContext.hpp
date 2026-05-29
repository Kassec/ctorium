#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include "BeanContext.hpp"
#include "Errors.hpp"

namespace ctr {

/**
 * @brief Context bound to a user scope key.
 *
 * Inherits standard resolution and lifecycle operations from BeanContext.
 */
class ScopedContext : public BeanContext {
public:
    /**
     * @brief Attaches mutable user data to this scope.
     * @tparam T User data type.
     * @param value User data reference.
     * @return Current scoped context for chaining.
     */
    template <class T>
    ScopedContext& setUserData(T& value);

    /**
     * @brief Clears user data from this scope.
     * @param value Must be nullptr.
     * @return Current scoped context for chaining.
     */
    ScopedContext& setUserData(std::nullptr_t value);

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
     * @brief Restarts this scoped context.
     * @return Current scoped context for chaining.
     */
    ScopedContext& restart();

    /**
     * @brief Resolves or creates a scoped context by stable key under the owning root.
     * @param key Stable user key.
     * @return Scoped child context.
     */
    ScopedContext& resolveScope(std::string_view key);

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
    BeanContext* root_;
};

} // namespace ctr
