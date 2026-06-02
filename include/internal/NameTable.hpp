#pragma once

#include <flat_map>
#include <vector>

#include "../api/ctr/Config.hpp"

#include "DescriptorId.hpp"
#include "NameId.hpp"

namespace CTORIUM_NAMESPACE::detail {

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

} // namespace CTORIUM_NAMESPACE::detail
