#pragma once

#include <functional>
#include <string_view>

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/// Transparent hash allowing unordered_map<string,...> to be queried with string_view,
/// eliminating heap allocation on lookup hot paths.
/// Provides both string and string_view overloads: GCC 16 requires explicit overloads
/// for all stored-key types to enable heterogeneous lookup in unordered containers.
struct StringViewHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(std::string_view{s});
    }
};

} // namespace CTORIUM_NAMESPACE::detail
