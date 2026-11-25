#pragma once

#include "sha256.hpp"

#include <cstdint>
#include <string_view>

namespace unbound {

enum class PackageIntegrityDecision {
    Valid, Cancelled, InvalidContent, ReadFailure, MetadataMismatch
};

namespace package_verification_detail {
inline char lowerHex(char value) {
    return value >= 'A' && value <= 'F' ? static_cast<char>(value + ('a' - 'A')) : value;
}
inline bool digest(std::string_view value) {
    if (value.size() != 64) return false;
    for (const auto character : value) {
        const auto lower = lowerHex(character);
        if (!((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'f'))) return false;
    }
    return true;
}
inline bool sameDigest(std::string_view first, std::string_view second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index)
        if (lowerHex(first[index]) != lowerHex(second[index])) return false;
    return true;
}
} // namespace package_verification_detail

// Hash the complete saved file with FileHashOptions::expectedBytes == 0, then
// apply the manifest size here. A correct hash and a wrong declared size mean
// the release metadata needs correction; deleting and downloading the same
// verified bytes cannot fix it. Read errors/cancellation never authorize cache
// deletion. No metadata shortcut can authorize installation without a full hash.
inline PackageIntegrityDecision classifyPackageIntegrity(
    const FileHashResult& read, std::string_view actualDigest,
    std::string_view expectedDigest, std::uint64_t expectedBytes = 0) {
    if (read.status == FileHashStatus::Cancelled) return PackageIntegrityDecision::Cancelled;
    if (!package_verification_detail::digest(expectedDigest))
        return PackageIntegrityDecision::ReadFailure;
    if (!read.ok()) {
        if (read.status == FileHashStatus::SizeMismatch ||
            read.status == FileHashStatus::Truncated ||
            read.status == FileHashStatus::SizeChanged)
            return PackageIntegrityDecision::InvalidContent;
        return PackageIntegrityDecision::ReadFailure;
    }
    if (read.bytes != read.total ||
        !package_verification_detail::digest(actualDigest) ||
        !package_verification_detail::sameDigest(actualDigest, expectedDigest))
        return PackageIntegrityDecision::InvalidContent;
    if (expectedBytes && read.total != expectedBytes)
        return PackageIntegrityDecision::MetadataMismatch;
    return PackageIntegrityDecision::Valid;
}

} // namespace unbound
