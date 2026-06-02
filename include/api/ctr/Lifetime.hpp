#pragma once

#include <cstdint>

#include "Config.hpp"

namespace CTORIUM_NAMESPACE {

/**
 * @brief Scope lifetime governing instance sharing and destruction.
 */
enum class Lifetime : std::uint8_t {
    Prototype,
    Singleton,
    Session,
    ThreadLocal,
};

} // namespace CTORIUM_NAMESPACE
