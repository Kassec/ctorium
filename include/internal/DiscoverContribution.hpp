#pragma once

#include <span>

#include "ContributedDescriptor.hpp"

namespace ctr::detail {

/**
 * @brief Discover contribution transmitted to the registry. Static lifetime, no copy.
 */
using DiscoverContribution = std::span<const ContributedDescriptor>;

} // namespace ctr::detail
