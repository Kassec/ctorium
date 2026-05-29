#pragma once

#include <utility>

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
 */
class ListenerHandle {
public:
    /** @brief Function type used internally by integration code to remove a listener. */
    using RemoveFn = void (*)(void*) noexcept;

    /**
     * @brief Builds an empty handle.
     */
    constexpr ListenerHandle() noexcept = default;

    /**
     * @brief Builds a handle from an opaque token and remover function.
     * @param token Opaque listener token.
     * @param remover Opaque removal callback.
     */
    constexpr ListenerHandle(void* token, RemoveFn remover) noexcept
        : token_(token), remover_(remover) {}

    ListenerHandle(const ListenerHandle&) = default;
    ListenerHandle(ListenerHandle&&) noexcept = default;
    ListenerHandle& operator=(const ListenerHandle&) = default;
    ListenerHandle& operator=(ListenerHandle&&) noexcept = default;
    ~ListenerHandle() = default;

    /**
     * @brief Unregisters the listener if still active.
     *
     * This operation is idempotent.
     */
    void remove() noexcept {
        if (token_ != nullptr && remover_ != nullptr) {
            remover_(token_);
            token_ = nullptr;
            remover_ = nullptr;
        }
    }

private:
    void* token_ = nullptr;
    RemoveFn remover_ = nullptr;
};

} // namespace ctr
