#pragma once

#include "config.hpp"

#include <cstdint>

namespace unbound {

enum class ArchiveSizeLimit { None, Entry, Expanded };

// This predicate never adds untrusted ZIP sizes. Subtract only after checking
// the accumulated total, so corrupt metadata cannot wrap the expanded budget.
inline constexpr ArchiveSizeLimit archiveSizeLimit(std::uint64_t expanded,
                                                 std::uint64_t entry) {
    if (entry > config::MaxArchiveEntryBytes) return ArchiveSizeLimit::Entry;
    if (expanded > config::MaxArchiveExpandedBytes ||
        entry > config::MaxArchiveExpandedBytes - expanded)
        return ArchiveSizeLimit::Expanded;
    return ArchiveSizeLimit::None;
}

} // namespace unbound
