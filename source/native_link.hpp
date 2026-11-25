#pragma once

#include "inherited_content.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace unbound {

// FSMakeLink, as established from coreinit.rpl (SDK 2.12.13, 00050010-1000800A):
//
//   FSStatus FSMakeLink(FSClient*, FSCmdBlock*, const char* path,
//                       uint32_t first, uint32_t second, uint64_t third,
//                       const void* block16 /* optional */, FSErrorFlag);
//
// FSMakeLink 0x020614e0 forwards r5-r7 and r9:r10 to FSMakeLinkAsync 0x0205d640;
// r8 is skipped because the 64-bit argument starts on an odd register pair.
// __FSAShimSetupRequestMakeLink 0x02051168 builds FSA command 0x75 below and
// zero-fills the block when its pointer is null. No public header, OS caller,
// emulator or readable IOSU image defines the MEANING of the three integers or
// the block. A WFS link entry stores metadata only (flags, size, owner, group,
// mode, times) and no target: it resolves by its own path in the lower title.
//
// Production must never call FSMakeLink with inferred arguments. This gate stays
// false until a hardware calibration proves a created entry is indistinguishable
// from an installer-made sibling (full FSA stat) and that the running game reads
// the BASE file through it. Until then a missing stock child is copied.
inline constexpr bool NativeLinkArgumentsVerified = false;
inline constexpr std::uint32_t NativeLinkCommand = 0x75;

struct NativeLinkRequestLayout {
    char path[0x280];
    std::uint32_t first;
    std::uint32_t second;
    std::uint64_t third;
    std::uint8_t block[16];
};
static_assert(offsetof(NativeLinkRequestLayout, first) == 0x280);
static_assert(offsetof(NativeLinkRequestLayout, second) == 0x284);
static_assert(offsetof(NativeLinkRequestLayout, third) == 0x288);
static_assert(offsetof(NativeLinkRequestLayout, block) == 0x290);
static_assert(sizeof(NativeLinkRequestLayout) == 0x2a0);

enum class NativeLinkRefusal {
    None,
    Unverified,      // argument contract not calibrated on hardware
    UnsafePath,
    NotOwned,        // parent is not an HQ-owned real UPDATE directory
    Occupied,        // something already exists; never replace a copy or payload
    NoOriginal,      // BASE has no entry of the same type at the same path
    LegacyReference, // a retained .unbound-link-* entry must not be recreated
};

struct NativeLinkCandidate {
    std::string updateParent;   // real directory under the verified UPDATE content
    std::string name;
    bool parentOwned = false;   // valid HQFOLDERLINK1 owner covers updateParent
    InheritedStat parent;       // lstat of updateParent
    InheritedStat existing;     // lstat of updateParent/name
    InheritedStat original;     // lstat of the same relative path in BASE
};

// Rules a future sparse writer must pass for EVERY entry, including folders
// converted by r35-r41. A copy or payload that already exists is authoritative:
// it is replaced only by a separate, journaled compaction step that confirms the
// bytes equal BASE first, never by this check.
inline NativeLinkRefusal nativeLinkRefusal(const NativeLinkCandidate& candidate,
                                           bool argumentsVerified = NativeLinkArgumentsVerified) {
    if (!argumentsVerified) return NativeLinkRefusal::Unverified;
    const auto path = candidate.updateParent + "/" + candidate.name;
    if (!inherited_detail::root(candidate.updateParent) || candidate.name.empty() ||
        candidate.name.size() > 255 || candidate.name.find_first_of("/\\:") != std::string::npos ||
        candidate.name == "." || candidate.name == ".." || path.size() > 639)
        return NativeLinkRefusal::UnsafePath;
    for (const unsigned char ch : candidate.name)
        if (ch < 0x20 || ch == 0x7f) return NativeLinkRefusal::UnsafePath;
    if (!inherited_detail::component(candidate.name)) return NativeLinkRefusal::LegacyReference;
    if (!candidate.parentOwned || candidate.parent.kind != InheritedEntryKind::Directory)
        return NativeLinkRefusal::NotOwned;
    if (candidate.existing.kind != InheritedEntryKind::Missing) return NativeLinkRefusal::Occupied;
    if (candidate.original.kind != InheritedEntryKind::Regular &&
        candidate.original.kind != InheritedEntryKind::Directory)
        return NativeLinkRefusal::NoOriginal;
    return NativeLinkRefusal::None;
}

} // namespace unbound
