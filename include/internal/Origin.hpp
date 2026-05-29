#pragma once

#include <cstdint>

namespace ctr::detail {

/**
 * @brief Source of a descriptor contribution.
 */
enum class Origin : std::uint8_t {
    AnnotatedType,
    FactoryProduct,
    RuntimeBinding,
};

} // namespace ctr::detail
