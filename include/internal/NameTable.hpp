#pragma once

#include <flat_map>
#include <vector>

#include "DescriptorId.hpp"
#include "NameId.hpp"

namespace ctr::detail {

/**
 * @brief Per-name candidate entry. Candidates are sorted by descending priority at start().
 */
struct NameEntry {
    std::vector<DescriptorId> candidates;
    DescriptorId head = kInvalidDescriptorId;
    bool ambiguous = false;
};

/**
 * @brief Per-type candidate index.
 */
struct NameTable {
    std::flat_map<NameId, NameEntry> entries;
};

} // namespace ctr::detail
