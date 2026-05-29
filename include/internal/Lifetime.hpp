#pragma once

#include <cstdint>

namespace ctr::detail {

/**
 * @brief Scope lifetime governing instance sharing and destruction.
 */
enum class Lifetime : std::uint8_t {
    Prototype,
    Singleton,
    Session,
    ThreadLocal,
};

} // namespace ctr::detail
