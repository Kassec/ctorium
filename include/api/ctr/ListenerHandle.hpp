#pragma once

#include <cstddef>

// Forward declaration: remove() calls ListenerStore methods.
// The definition of remove() lives in ListenerStore.hpp (which includes this header).
namespace ctr::detail { class ListenerStore; }

namespace ctr {

/**
 * @brief Listener phase tag for context initialization.
 */
struct onInitialized_t {};
/** @brief Listener phase value for context initialization. */
inline constexpr onInitialized_t onInitialized{};

/**
 * @brief Listener phase tag for bean creation.
 */
struct onCreated_t {};
/** @brief Listener phase value for bean creation. */
inline constexpr onCreated_t onCreated{};

/**
 * @brief Listener phase tag for pre-destruction notifications.
 */
struct onPreDestroy_t {};
/** @brief Listener phase value for pre-destruction notifications. */
inline constexpr onPreDestroy_t onPreDestroy{};

/**
 * @brief Listener phase tag for bean destruction.
 */
struct onDestroyed_t {};
/** @brief Listener phase value for bean destruction. */
inline constexpr onDestroyed_t onDestroyed{};

/**
 * @brief Opaque handle returned by listener registration.
 *
 * Destroying this object does not unregister the listener.
 *
 * ### Copy semantics
 * Copies of a handle share the same logical registration.  The first `remove()`
 * call on any copy unregisters the listener; subsequent calls on other copies
 * are safe no-ops (the token is already absent from the store).
 *
 * ### Lifetime note
 * `remove()` is undefined behaviour if the owning `BeanContext` has been
 * stopped. `BeanContext::stop()` is terminal and destroys the owning context,
 * so any surviving handle becomes dangling after `stop()` returns.
 */
class ListenerHandle {
public:
    /**
     * @brief Builds an empty (no-op) handle.
     */
    constexpr ListenerHandle() noexcept = default;

    ListenerHandle(const ListenerHandle&) = default;
    ListenerHandle(ListenerHandle&&) noexcept = default;
    ListenerHandle& operator=(const ListenerHandle&) = default;
    ListenerHandle& operator=(ListenerHandle&&) noexcept = default;
    ~ListenerHandle() = default;

    /**
     * @brief Unregisters the listener if still active.
     *
     * Safe to call multiple times (idempotent) while the owning context is
     * alive. Calling this after the owning `BeanContext` has been stopped is
     * undefined behaviour.
     * Defined in ListenerStore.hpp (requires the full ListenerStore definition).
     */
    void remove() noexcept;

private:
    friend class ctr::detail::ListenerStore;

    // Internal constructor used by ListenerStore::addListener().
    explicit ListenerHandle(ctr::detail::ListenerStore* store,
                            std::size_t token) noexcept
        : store_(store), tokenValue_(token) {}

    ctr::detail::ListenerStore* store_ = nullptr;
    std::size_t                 tokenValue_ = 0;
};

} // namespace ctr
