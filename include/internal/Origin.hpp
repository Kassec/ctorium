#pragma once

#include <cstdint>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Source of a descriptor contribution.
 */
enum class Origin : std::uint8_t {
    AnnotatedType,
    FactoryProduct,
    RuntimeBinding,
};

} // namespace CTORIUM_NAMESPACE::detail
