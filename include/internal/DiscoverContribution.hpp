#pragma once

#include <span>

#include "../api/ctr/Config.hpp"

#include "ContributedDescriptor.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Discover contribution transmitted to the registry. Static lifetime, no copy.
 */
using DiscoverContribution = std::span<const ContributedDescriptor>;

} // namespace CTORIUM_NAMESPACE::detail
