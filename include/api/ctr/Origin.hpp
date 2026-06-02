#pragma once

#include <cstdint>

#include "Config.hpp"

namespace CTORIUM_NAMESPACE {

/**
 * @brief Source of a descriptor contribution.
 */
enum class Origin : std::uint8_t {
    AnnotatedType,
    FactoryProduct,
    RuntimeBinding,
};

} // namespace CTORIUM_NAMESPACE
