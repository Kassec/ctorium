#pragma once

#include <cstdint>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Scope lifetime governing instance sharing and destruction.
 */
enum class Lifetime : std::uint8_t {
    Prototype,
    Singleton,
    Session,
    ThreadLocal,
};

} // namespace CTORIUM_NAMESPACE::detail
