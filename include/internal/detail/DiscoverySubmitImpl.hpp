#pragma once

#include <span>

#include "DescriptorGen.hpp"
#include "Registry.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Options.hpp"

namespace ctr {

// -------------------------------------------------------------------------
// detail::submitDiscoveryContribution
// -------------------------------------------------------------------------

namespace detail {
template <auto... Roots>
inline void submitDiscoveryContribution(BeanContext& context, DiscoverOptions options) {
    context.assertCanDiscover_();
    if (options.retainAllMetadata) {
        static constexpr auto kDescriptors =
            std::define_static_array(makeAllDescriptors<true, Roots...>());
        context.core().submitContribution(
            std::span<const ContributedDescriptor>(kDescriptors.data(), kDescriptors.size()),
            options);
    } else {
        static constexpr auto kDescriptors =
            std::define_static_array(makeAllDescriptors<false, Roots...>());
        context.core().submitContribution(
            std::span<const ContributedDescriptor>(kDescriptors.data(), kDescriptors.size()),
            options);
    }
}

} // namespace detail

} // namespace ctr
