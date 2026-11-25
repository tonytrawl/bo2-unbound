#include "archive_limits.hpp"
#include "tester_archive.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

int main() {
    using unbound::ArchiveSizeLimit;
    using unbound::archiveSizeLimit;
    constexpr std::uint64_t GiB = 1024ULL * 1024 * 1024;
    static_assert(unbound::config::MaxArchiveBytes == 4 * GiB - 1);
    static_assert(unbound::config::MaxArchiveEntryBytes == 2 * GiB);
    static_assert(unbound::config::MaxArchiveExpandedBytes == 5 * GiB);

    assert(archiveSizeLimit(0, 0) == ArchiveSizeLimit::None);
    assert(archiveSizeLimit(0, 2 * GiB) == ArchiveSizeLimit::None);
    assert(archiveSizeLimit(0, 2 * GiB + 1) == ArchiveSizeLimit::Entry);
    assert(archiveSizeLimit(3 * GiB, 2 * GiB) == ArchiveSizeLimit::None);
    assert(archiveSizeLimit(3 * GiB + 1, 2 * GiB) == ArchiveSizeLimit::Expanded);
    assert(archiveSizeLimit(5 * GiB, 0) == ArchiveSizeLimit::None);
    assert(archiveSizeLimit(5 * GiB, 1) == ArchiveSizeLimit::Expanded);
    assert(archiveSizeLimit(5 * GiB + 1, 0) == ArchiveSizeLimit::Expanded);
    assert(archiveSizeLimit(std::numeric_limits<std::uint64_t>::max(), 1) ==
           ArchiveSizeLimit::Expanded);
    assert(archiveSizeLimit(0, std::numeric_limits<std::uint64_t>::max()) ==
           ArchiveSizeLimit::Entry);

    // The published 0.2.0 package expands 8,064,802 bytes beyond the old total
    // ceiling. Exercise that exact aggregate without allocating a giant file.
    std::uint64_t expanded = 0;
    for (const std::uint64_t entry : {GiB / 2, GiB / 2, GiB / 2, GiB / 2,
                                      std::uint64_t{8064802}}) {
        assert(archiveSizeLimit(expanded, entry) == ArchiveSizeLimit::None);
        expanded += entry;
    }
    assert(expanded == 2155548450ULL);
    assert(expanded > 2 * GiB);

    // Five GiB is accepted across ordinary entries, but never a six-GiB total.
    expanded = 0;
    for (const std::uint64_t entry : {2 * GiB, 2 * GiB, GiB}) {
        assert(archiveSizeLimit(expanded, entry) == ArchiveSizeLimit::None);
        expanded += entry;
    }
    assert(expanded == 5 * GiB);
    assert(archiveSizeLimit(expanded, GiB) == ArchiveSizeLimit::Expanded);
    // The combined tester exceeds signed 32-bit offsets but fits FAT32.
    constexpr std::uint64_t combinedZipBytes = 3520339520ULL;
    constexpr std::uint64_t combinedExpandedBytes = 4755944782ULL;
    assert(combinedZipBytes > std::uint64_t{std::numeric_limits<std::int32_t>::max()});
    assert(combinedZipBytes <= unbound::config::MaxArchiveBytes);
    assert(combinedExpandedBytes <= unbound::config::MaxArchiveExpandedBytes);
    assert(unbound::testerReadmeEntry("README_TESTER.txt", false, 1138));
    assert(!unbound::testerReadmeEntry("README_TESTER.txt", true, 0));
    assert(!unbound::testerReadmeEntry("README_TESTER.txt", false, 65537));
    assert(!unbound::testerReadmeEntry("update/content/README_TESTER.txt", false, 1138));
    assert(!unbound::testerReadmeEntry("../README_TESTER.txt", false, 1138));
    assert(!unbound::testerReadmeEntry("unrelated.txt", false, 1138));
    std::puts("Archive limit tests passed");
}
