#pragma once

#include "directory_policy.hpp"

#include <cerrno>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace unbound {

enum class FolderLinkEntry { Missing, Directory, Link, Other, Error };
struct FolderLinkState {
    FolderLinkEntry kind = FolderLinkEntry::Missing;
    int error = 0;
};
enum class FolderLinkOwnership { Missing, Valid, Invalid };

struct FolderLinkPaths {
    std::string target;
    std::string original() const { return target + ".unbound-link-original"; }
    std::string staged() const { return target + ".unbound-link-new"; }
    std::string journal() const { return target + ".unbound-link-state"; }
    std::string complete() const { return target + ".unbound-link-complete"; }
    // A converted directory moved aside while its retained reference returns.
    std::string retired() const { return target + ".unbound-link-retired"; }
    std::string ownershipText() const { return "HQFOLDERLINK1\ntarget=" + target + "\n"; }
};

// The caller derives alternateKnownTarget from the verified SAME title and
// relative content path on the other known storage device. The record is never
// decoded into an I/O path. Moving an installed title can retain a V1 record's
// old device prefix; accept that exact text only while its opaque reference is
// still present, with an identical suffix after the device prefix.
inline bool inheritedOwnershipTextMatches(const std::string& text,
                                          const std::string& currentTarget,
                                          const std::string& alternateKnownTarget,
                                          bool originalOrTargetIsLink) {
    const auto valid = [](const std::string& path) {
        if (path.empty() || path.size() > 639 || path.back() == '/' ||
            path.find('\\') != std::string::npos || path.find("//") != std::string::npos ||
            path.find("/../") != std::string::npos || path.find("/./") != std::string::npos ||
            path.ends_with("/..") || path.ends_with("/.")) return false;
        for (const unsigned char ch : path) if (ch < 0x20 || ch == 0x7f) return false;
        std::size_t start = 0;
        while (start < path.size()) {
            const auto end = path.find('/', start);
            if ((end == std::string::npos ? path.size() : end) - start > 255) return false;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return true;
    };
    if (!valid(currentTarget)) return false;
    if (text == FolderLinkPaths{currentTarget}.ownershipText()) return true;
    if (!originalOrTargetIsLink || !valid(alternateKnownTarget) ||
        alternateKnownTarget == currentTarget) return false;
    const auto currentDevice = currentTarget.find(":/");
    const auto alternateDevice = alternateKnownTarget.find(":/");
    if (currentDevice == std::string::npos || alternateDevice == std::string::npos ||
        currentTarget.substr(currentDevice) != alternateKnownTarget.substr(alternateDevice) ||
        currentTarget.substr(0, currentDevice) == alternateKnownTarget.substr(0, alternateDevice)) return false;
    return text == FolderLinkPaths{alternateKnownTarget}.ownershipText();
}

inline constexpr std::size_t MaximumContentFolderLinkCandidates = 4096;

// Derive candidates from mapped payload directories, never raw manifest paths.
// The lexical set naturally orders a parent before any of its descendants.
// Code/meta and the content root itself are deliberately outside this policy.
inline bool addContentFolderLinkPrefixes(const std::string& contentRoot,
                                         const std::string& requiredDirectory,
                                         std::set<std::string>& candidates, std::string& error) {
    error.clear();
    if (contentRoot.empty() || contentRoot.back() == '/' || requiredDirectory.empty()) {
        error = "The validated content root is invalid.";
        return false;
    }
    if (requiredDirectory == contentRoot || requiredDirectory.rfind(contentRoot + "/", 0) != 0)
        return true;
    std::vector<std::string> prefixes;
    std::string current = contentRoot;
    std::size_t start = contentRoot.size() + 1;
    while (start <= requiredDirectory.size()) {
        const auto end = requiredDirectory.find('/', start);
        const auto component = requiredDirectory.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        std::string folded = component;
        for (char& value : folded) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (component.empty() || component == "." || component == ".." ||
            component.find_first_of(":\\") != std::string::npos ||
            folded.find(".unbound-link-") != std::string::npos || prefixes.size() >= 20) {
            error = "The package has an unsupported content directory path.";
            return false;
        }
        for (const unsigned char value : component) {
            if (value < 0x20 || value == 0x7f) {
                error = "The package has an invalid content directory name.";
                return false;
            }
        }
        current += "/" + component;
        prefixes.push_back(current);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::size_t additions = 0;
    for (const auto& prefix : prefixes) if (!candidates.count(prefix)) ++additions;
    if (candidates.size() > MaximumContentFolderLinkCandidates ||
        additions > MaximumContentFolderLinkCandidates - candidates.size()) {
        error = "The package requires too many distinct content directories.";
        return false;
    }
    candidates.insert(prefixes.begin(), prefixes.end());
    return true;
}

// Stock-directory inheritance must be preserved explicitly before a virtual
// directory is replaced. A broad SDK fallback description is NOT proof that an
// empty real directory retains omitted children. Never unlink the opaque
// reference, follow it for writes, or manufacture another link.
//
// Caller must allowlist a mapped payload-required core-content path. IO performs only
// verified stock copies plus small metadata operations under that path's parent.
// The static owner record remains compatible with r35/r36. A separate completion
// hook is committed only after verified child coverage and directory activation.
template<class Io>
bool materializeTitleFolderLink(const FolderLinkPaths& paths, Io& io,
                                std::string& error, const DirectoryActivity& activity = {}) {
    error.clear();
    const auto fail = [&](const char* operation, const std::string& path, int code) {
        error = "Could not prepare an update content folder.\n" + std::string(operation) + ": " +
                path + "; errno=" + std::to_string(code ? code : EIO) + " (" +
                std::strerror(code ? code : EIO) + "). The original game files were not changed.";
        return false;
    };
    const auto inspect = [&](const std::string& path) {
        if (activity) activity("stat", path);
        return io.inspect(path);
    };
    const auto rename = [&](const std::string& from, const std::string& to) {
        if (activity) activity("rename", from + " -> " + to);
        return io.rename(from, to);
    };
    if (paths.target.empty()) return fail("validate owned path", paths.target, EINVAL);
    // Journal commit also uses .unbound-new/.unbound-old. Check every recovery
    // suffix before copying a potentially large stock tree, not at its final
    // rename. Native WUT paths allow 639 bytes and components allow 255.
    const auto longestRecoveryPath = paths.target + ".unbound-link-complete.unbound-new";
    const auto lastSlash = longestRecoveryPath.find_last_of('/');
    if (longestRecoveryPath.size() > 639 ||
        longestRecoveryPath.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1).size() > 255)
        return fail("owned recovery path exceeds native limits", paths.target, ENAMETOOLONG);
    auto target = inspect(paths.target);
    auto original = inspect(paths.original());
    auto staged = inspect(paths.staged());
    for (const auto& check : {std::pair{paths.target, target}, std::pair{paths.original(), original},
                              std::pair{paths.staged(), staged}})
        if (check.second.kind == FolderLinkEntry::Error)
            return fail("stat", check.first, check.second.error);
    if (activity) activity("check ownership", paths.journal());
    const auto ownership = io.readOwnership(paths, error);
    if (ownership == FolderLinkOwnership::Invalid) return false;
    bool originalValidated = false;
    if (ownership == FolderLinkOwnership::Missing) {
        if (original.kind != FolderLinkEntry::Missing || staged.kind != FolderLinkEntry::Missing)
            return fail("unowned recovery entry was retained", paths.target, EEXIST);
        if (target.kind == FolderLinkEntry::Directory || target.kind == FolderLinkEntry::Missing) return true;
        if (target.kind != FolderLinkEntry::Link)
            return fail("required folder is neither directory nor patch reference", paths.target, ENOTDIR);
        if (!io.validateOriginalContent(error)) return false;
        // The complete inventory and free-space budget must be known before
        // even the first ownership/staging write. No empty-folder fallback.
        if (!io.preflightInheritedDirectory(paths, false, error)) return false;
        originalValidated = true;
        if (activity) activity("save folder repair state", paths.journal());
        if (!io.writeOwnership(paths, error)) return false;
    }
    if ((original.kind != FolderLinkEntry::Missing && original.kind != FolderLinkEntry::Link) ||
        (staged.kind != FolderLinkEntry::Missing && staged.kind != FolderLinkEntry::Directory))
        return fail("unexpected owned recovery entry was retained", paths.target, EINVAL);
    if (target.kind == FolderLinkEntry::Directory && original.kind == FolderLinkEntry::Link &&
        staged.kind == FolderLinkEntry::Missing) {
        // r35/r36 already activated an empty directory. A current version marker
        // does not make its missing stock children safe. Repair only missing
        // children; existing payload/user files remain authoritative.
        if (!io.validateOriginalContent(error) ||
            !io.preflightInheritedDirectory(paths, true, error) ||
            !io.populateInheritedDirectory(paths, true, error)) return false;
        return io.markInheritedComplete(paths, error);
    }

    // A cut between the two renames leaves the reference under its reserved
    // name. Restore it first, so every subsequent attempt starts from the same
    // explicit state and never treats an absent directory as a fresh install.
    if (target.kind == FolderLinkEntry::Missing && original.kind == FolderLinkEntry::Link) {
        const int restored = rename(paths.original(), paths.target);
        if (restored) return fail("restore original patch reference", paths.original(), restored);
        target = inspect(paths.target);
        original = inspect(paths.original());
        if (target.kind != FolderLinkEntry::Link || original.kind != FolderLinkEntry::Missing)
            return fail("restored patch reference failed verification", paths.target, EIO);
    }
    if (target.kind != FolderLinkEntry::Link || original.kind != FolderLinkEntry::Missing)
        return fail("unexpected folder repair state was retained", paths.target, EINVAL);
    if (!originalValidated && !io.validateOriginalContent(error)) return false;
    if (!originalValidated && !io.preflightInheritedDirectory(paths, false, error)) return false;
    if (staged.kind == FolderLinkEntry::Missing) {
        if (activity) activity("stage update directory", paths.staged());
        const int made = io.makeDirectory(paths.staged());
        if (made) return fail("create staged update directory", paths.staged(), made);
        staged = inspect(paths.staged());
    }
    if (staged.kind != FolderLinkEntry::Directory)
        return fail("staged update directory failed verification", paths.staged(), ENOTDIR);
    // Retry may find a partially populated owned stage. Every existing staged
    // file must be reverified or rewritten by this hook, never accepted merely
    // because its name or length exists. It must prove ALL inherited children
    // before the live reference is moved.
    if (activity) activity("preserve inherited content", paths.staged());
    if (!io.populateInheritedDirectory(paths, false, error)) return false;

    const int preserved = rename(paths.target, paths.original());
    if (preserved) return fail("preserve original patch reference", paths.target, preserved);
    target = inspect(paths.target);
    original = inspect(paths.original());
    if (target.kind != FolderLinkEntry::Missing || original.kind != FolderLinkEntry::Link)
        return fail("preserved patch reference failed verification", paths.original(), EIO);
    const int committed = rename(paths.staged(), paths.target);
    if (committed) {
        const auto current = inspect(paths.target);
        if (current.kind == FolderLinkEntry::Missing) {
            const int restored = rename(paths.original(), paths.target);
            if (restored)
                return fail("restore preserved reference; retry HQ before launching", paths.original(), restored);
        }
        return fail("activate update directory; reference retained", paths.staged(), committed);
    }
    target = inspect(paths.target);
    original = inspect(paths.original());
    staged = inspect(paths.staged());
    if (target.kind != FolderLinkEntry::Directory || original.kind != FolderLinkEntry::Link ||
        staged.kind != FolderLinkEntry::Missing)
        return fail("activated update directory failed verification", paths.target, EIO);
    return io.markInheritedComplete(paths, error);
}

} // namespace unbound
