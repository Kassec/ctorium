#pragma once

#include "Config.hpp"

#include "Markers.hpp"

namespace CTORIUM_NAMESPACE {

/**
 * @brief Controls reflection discovery behavior.
 */
struct DiscoverOptions {
    /**
     * @brief Keeps all visible reflection metadata for provided roots when set to true.
     *
     * When false, only metadata required by the DI engine is retained.
     *
     * When true, metadata retention cost is higher, but discovery still remains bounded
     * to the visible perimeter of provided roots (no global or external scan).
     */
    bool retainAllMetadata = false;
};

/**
 * @brief Controls explicit runtime bindings.
 */
struct BindOptions {
    /**
     * @brief Named selector for the binding.
     *
     * A nullptr or empty string means no named selector.
     */
    const char* name = nullptr;

    /**
     * @brief Priority used to arbitrate among candidates.
     */
    int priority = 0;
};

/**
 * @brief Controls listener registration.
 */
struct ListenerOptions {
    /**
     * @brief Priority used to order listeners.
     */
    int priority = 0;
};

} // namespace CTORIUM_NAMESPACE
