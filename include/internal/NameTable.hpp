#pragma once

#include <flat_map>
#include <vector>

#include "DescriptorId.hpp"
#include "NameId.hpp"

namespace ctr::detail {

/**
 * @brief Per-type candidate index. Vectors are sorted by descending priority at start().
 */
struct NameTable {
    std::flat_map<NameId, std::vector<DescriptorId>> entries;
};

} // namespace ctr::detail
