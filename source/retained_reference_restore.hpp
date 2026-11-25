#pragma once

#include "inherited_content.hpp"
#include "title_folder_link.hpp"

#include <cerrno>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace unbound {

// Earlier HQ builds converted an inherited UPDATE folder by renaming its native
// reference to <target>.unbound-link-original and activating a real directory:
// empty in r35/r36, a verified copy of the BASE subtree from r37. Where the game
// has its own fallback for the Unbound files (shared movies: <language>/video is
// searched first), the converted folder is unnecessary. Putting the retained
// reference back under its own name returns the title to its native layout. No
// link is manufactured: a native reference stores no target and resolves by its
// current path, which is again the path it was created for.
//
// Only HQ-owned state is touched. The caller validates the ownership record,
// both title roots and the relocation root before planning.
enum class RetainedReferenceState {
    Converted,      // target real directory, reference aside
    ReferenceAside, // interrupted: directory retired, reference not yet back
    Restored,       // reference back; retired directory still holds content
    StageAside,     // interrupted r37 conversion: stage built, reference aside
    StageLeft,      // reference in place; abandoned private stage remains
    Clean,          // native layout; only owner records may remain
};

enum class RetainedChildAction { RemoveIfIdentical, Relocate, Keep };
struct RetainedChild {
    std::string relative;
    std::uint64_t bytes = 0;
    bool directory = false;
    RetainedChildAction action = RetainedChildAction::Keep;
    std::string compareWith; // RemoveIfIdentical: the copy must equal this file
    std::string destination; // Relocate: normal installed location
};

struct RetainedReferencePlan {
    RetainedReferenceState state = RetainedReferenceState::Clean;
    std::string stockRoot;
    std::string relocationRoot; // empty: nothing may be relocated
    std::vector<RetainedChild> items; // breadth-first; parents before children
    std::uint64_t reclaimBytes = 0;   // HQ-made copies that may be freed
    std::uint64_t relocateBytes = 0;
    unsigned kept = 0;
    bool records = false;
    // Set when a converted folder holds a game-visible file with nowhere to
    // go. Restoring would hide it, so the working conversion is left alone.
    std::string blocked;

    // A retired folder holding only kept entries stays owned and reported,
    // but must not block every later launch.
    bool needed() const {
        if (!blocked.empty()) return false;
        if (state == RetainedReferenceState::Converted || state == RetainedReferenceState::ReferenceAside ||
            state == RetainedReferenceState::StageAside) return true;
        for (const auto& item : items)
            if (!item.directory && item.action != RetainedChildAction::Keep) return true;
        if (state == RetainedReferenceState::Clean) return records;
        return kept == 0;
    }
};

namespace retained_detail {
inline bool reservedName(const std::string& name) {
    const auto folded = inherited_detail::lower(name);
    if (folded.find(".unbound-link-") != std::string::npos) return true;
    for (const std::string ending : {".unbound-new", ".unbound-old", ".unbound-replacing", ".unbound-kept"})
        if (folded.ends_with(ending)) return true;
    return false;
}
inline bool plainName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos) return false;
    for (const unsigned char ch : name) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}
inline bool fits(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return path.size() <= 639 && path.size() - (slash == std::string::npos ? 0 : slash + 1) <= 255;
}
} // namespace retained_detail

inline std::string retainedTree(const FolderLinkPaths& paths, RetainedReferenceState state) {
    switch (state) {
    case RetainedReferenceState::Converted: return paths.target;
    case RetainedReferenceState::ReferenceAside:
    case RetainedReferenceState::Restored: return paths.retired();
    case RetainedReferenceState::StageAside:
    case RetainedReferenceState::StageLeft: return paths.staged();
    case RetainedReferenceState::Clean: break;
    }
    return {};
}

// READ-ONLY. Classifies by name, type and length only, so a startup check never
// reads multi-GB movie data. Removal is confirmed byte-for-byte when executed.
template<class Io>
bool planRetainedReferenceRestore(const FolderLinkPaths& paths, const std::string& stockRoot,
                                  const std::string& relocationRoot, Io& io,
                                  RetainedReferencePlan& output, std::string& error,
                                  const InheritedLimits& limits = {}) {
    error.clear();
    const auto fail = [&](const char* action, const std::string& path) {
        error = std::string("Original folder restore: ") + action + ": " + path;
        return false;
    };
    if (!inherited_detail::root(paths.target) || !inherited_detail::root(stockRoot) ||
        !retained_detail::fits(paths.retired() + ".unbound-new") || stockRoot == paths.target ||
        (!relocationRoot.empty() && (!inherited_detail::root(relocationRoot) ||
            relocationRoot == paths.target || inherited_detail::under(relocationRoot, paths.target) ||
            inherited_detail::under(paths.target, relocationRoot))))
        return fail("invalid restore roots", paths.target);
    using K = InheritedEntryKind;
    const auto target = io.inspect(paths.target), original = io.inspect(paths.original()),
               staged = io.inspect(paths.staged()), retired = io.inspect(paths.retired());
    for (const auto& [path, state] : {std::pair{paths.target, target}, std::pair{paths.original(), original},
                                     std::pair{paths.staged(), staged}, std::pair{paths.retired(), retired}})
        if (state.kind == K::Error) return fail("could not inspect", path);
    RetainedReferencePlan plan;
    plan.stockRoot = stockRoot;
    plan.records = io.recordsPresent(paths);
    const bool noStage = staged.kind == K::Missing, noRetired = retired.kind == K::Missing;
    if (target.kind == K::Directory && original.kind == K::Link && noStage && noRetired)
        plan.state = RetainedReferenceState::Converted;
    else if (target.kind == K::Missing && original.kind == K::Link && noStage && retired.kind == K::Directory)
        plan.state = RetainedReferenceState::ReferenceAside;
    else if (target.kind == K::Link && original.kind == K::Missing && noStage && retired.kind == K::Directory)
        plan.state = RetainedReferenceState::Restored;
    else if (target.kind == K::Missing && original.kind == K::Link && staged.kind == K::Directory && noRetired)
        plan.state = RetainedReferenceState::StageAside;
    else if (target.kind == K::Link && original.kind == K::Missing && staged.kind == K::Directory && noRetired)
        plan.state = RetainedReferenceState::StageLeft;
    else if (target.kind == K::Link && original.kind == K::Missing && noStage && noRetired)
        plan.state = RetainedReferenceState::Clean;
    else
        return fail("ambiguous folder state was retained for review", paths.target);
    // A private stage only ever held stock copies. Anything else stays put.
    const bool stage = plan.state == RetainedReferenceState::StageAside ||
                       plan.state == RetainedReferenceState::StageLeft;
    plan.relocationRoot = stage ? std::string{} : relocationRoot;
    const auto tree = retainedTree(paths, plan.state);
    if (tree.empty()) { output = std::move(plan); return true; }
    if (io.inspect(stockRoot).kind != K::Directory) return fail("original content folder is unavailable", stockRoot);

    struct Pending { std::string relative; unsigned depth; };
    std::vector<Pending> pending{{{}, 0}};
    std::set<std::string> claimed;
    for (std::size_t position = 0; position < pending.size(); ++position) {
        const auto current = pending[position];
        if (current.depth > limits.maximumDepth) return fail("folder depth limit exceeded", tree);
        const auto directory = inherited_detail::join(tree, current.relative);
        std::vector<std::string> names;
        if (!io.listNames(directory, names, error)) {
            if (error.empty()) fail("could not read", directory);
            return false;
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            if (!retained_detail::plainName(name)) return fail("unsafe entry name", directory + "/" + name);
            if (plan.items.size() >= limits.maximumEntries) return fail("entry limit exceeded", tree);
            RetainedChild item;
            item.relative = current.relative.empty() ? name : current.relative + "/" + name;
            const auto path = inherited_detail::join(tree, item.relative);
            const auto state = io.inspect(path);
            if (state.kind == K::Error) return fail("could not inspect", path);
            if (retained_detail::reservedName(name) || (state.kind != K::Directory && state.kind != K::Regular)) {
                // HQ working files, links and unknown types are never removed or moved.
                ++plan.kept;
                plan.items.push_back(std::move(item));
                continue;
            }
            if (state.kind == K::Directory) {
                item.directory = true;
                pending.push_back({item.relative, current.depth + 1});
                plan.items.push_back(std::move(item));
                continue;
            }
            item.bytes = state.bytes;
            const auto stockPath = inherited_detail::join(stockRoot, item.relative);
            const auto stock = io.inspect(stockPath);
            if (stock.kind == K::Error) return fail("could not inspect original", stockPath);
            if (stock.kind == K::Regular && stock.bytes == state.bytes) {
                item.action = RetainedChildAction::RemoveIfIdentical;
                item.compareWith = stockPath;
            } else if (!plan.relocationRoot.empty()) {
                const auto destination = inherited_detail::join(plan.relocationRoot, item.relative);
                const auto existing = io.inspect(destination);
                const auto folded = inherited_detail::lower(destination);
                if (existing.kind == K::Error) return fail("could not inspect", destination);
                if (!retained_detail::fits(destination + ".unbound-replacing") || !claimed.insert(folded).second) {
                    // Too long or colliding: keep it rather than guess.
                } else if (existing.kind == K::Missing) {
                    item.action = RetainedChildAction::Relocate;
                    item.destination = destination;
                } else if (existing.kind == K::Regular && existing.bytes == state.bytes) {
                    // Already installed at the normal location; the old one is a duplicate.
                    item.action = RetainedChildAction::RemoveIfIdentical;
                    item.compareWith = destination;
                }
            }
            if (item.action == RetainedChildAction::Keep) {
                ++plan.kept;
                if (plan.state == RetainedReferenceState::Converted && plan.blocked.empty())
                    plan.blocked = "an Unbound or user file has no free normal location: " + path;
            } else if (!inherited_detail::add(item.action == RetainedChildAction::Relocate ? plan.relocateBytes
                                                                                        : plan.reclaimBytes, item.bytes))
                return fail("size overflow", path);
            plan.items.push_back(std::move(item));
        }
    }
    output = std::move(plan);
    return true;
}

// IO adds rename(from,to), removeFile(path) and removeDirectory(path), each
// returning 0 or an errno; prepareDirectory(path,error) for a strict real
// relocation parent; sameContents(a,b,bytes,same,error) for a full comparison;
// removeRecords(paths,error) deleting the completion receipt, then the owner record.
template<class Io>
bool executeRetainedReferenceRestore(const FolderLinkPaths& paths, const RetainedReferencePlan& plan,
                                     Io& io, std::string& error) {
    error.clear();
    using K = InheritedEntryKind;
    const auto fail = [&](const std::string& action, const std::string& path, int code = 0) {
        error = "Original folder restore: " + action + ": " + path +
                (code ? "; errno=" + std::to_string(code) : std::string{});
        return false;
    };
    const auto relocate = [&](const std::string& tree, const RetainedChild& item) {
        const auto source = inherited_detail::join(tree, item.relative);
        const auto from = io.inspect(source), to = io.inspect(item.destination);
        if (from.kind == K::Missing && to.kind == K::Regular && to.bytes == item.bytes) return true; // retry
        if (from.kind != K::Regular || from.bytes != item.bytes || to.kind != K::Missing)
            return fail("file changed after planning; nothing was moved", source);
        const auto slash = item.destination.find_last_of('/');
        if (!io.prepareDirectory(item.destination.substr(0, slash), error)) return false;
        if (const int moved = io.rename(source, item.destination)) return fail("move to " + item.destination, source, moved);
        const auto placed = io.inspect(item.destination);
        if (placed.kind != K::Regular || placed.bytes != item.bytes || io.inspect(source).kind != K::Missing)
            return fail("moved file failed verification", item.destination);
        return true;
    };
    const auto putReferenceBack = [&](const std::string& tree) {
        if (const int restored = io.rename(paths.original(), paths.target)) {
            // Never leave the game without the folder: reactivate the directory.
            if (tree == paths.retired() && io.inspect(paths.target).kind == K::Missing)
                io.rename(paths.retired(), paths.target);
            return fail("restore original folder reference", paths.original(), restored);
        }
        if (io.inspect(paths.target).kind != K::Link || io.inspect(paths.original()).kind != K::Missing)
            return fail("restored folder reference failed verification", paths.target);
        return true;
    };
    std::string tree = retainedTree(paths, plan.state);
    switch (plan.state) {
    case RetainedReferenceState::Converted: {
        if (!plan.blocked.empty()) return fail("restore skipped; " + plan.blocked, paths.target);
        // Move Unbound files first; a failure here leaves the game layout unchanged.
        for (const auto& item : plan.items)
            if (item.action == RetainedChildAction::Relocate && !relocate(tree, item)) return false;
        if (const int retiredError = io.rename(paths.target, paths.retired()))
            return fail("set converted folder aside", paths.target, retiredError);
        if (io.inspect(paths.target).kind != K::Missing || io.inspect(paths.retired()).kind != K::Directory)
            return fail("converted folder move failed verification", paths.retired());
        tree = paths.retired();
        if (!putReferenceBack(tree)) return false;
        break;
    }
    case RetainedReferenceState::ReferenceAside:
    case RetainedReferenceState::StageAside:
        if (!putReferenceBack(tree)) return false;
        break;
    case RetainedReferenceState::Restored:
    case RetainedReferenceState::StageLeft:
    case RetainedReferenceState::Clean:
        break;
    }
    unsigned kept = plan.kept;
    if (!tree.empty()) {
        for (const auto& item : plan.items) {
            if (item.directory || item.action == RetainedChildAction::Keep) continue;
            const auto path = inherited_detail::join(tree, item.relative);
            if (item.action == RetainedChildAction::Relocate) {
                if (!relocate(tree, item)) return false;
                continue;
            }
            const auto current = io.inspect(path);
            if (current.kind == K::Missing) continue; // removed by an earlier attempt
            if (current.kind != K::Regular || current.bytes != item.bytes)
                return fail("file changed after planning; it was kept", path);
            bool same = false;
            if (!io.sameContents(path, item.compareWith, item.bytes, same, error)) return false;
            if (!same) {
                // Not a copy after all: never delete it. Keep it in effect at the
                // normal location when that is free, otherwise keep and report it.
                if (!plan.relocationRoot.empty() && item.compareWith != inherited_detail::join(plan.relocationRoot, item.relative)) {
                    RetainedChild moved = item;
                    moved.destination = inherited_detail::join(plan.relocationRoot, item.relative);
                    if (io.inspect(moved.destination).kind == K::Missing &&
                        retained_detail::fits(moved.destination + ".unbound-replacing")) {
                        if (!relocate(tree, moved)) return false;
                        continue;
                    }
                }
                // Mark it so later checks report it instead of comparing it forever.
                const auto marked = path + ".unbound-kept";
                if (!retained_detail::fits(marked) || io.inspect(marked).kind != K::Missing)
                    return fail("could not mark a kept file", path);
                if (const int markedError = io.rename(path, marked)) return fail("mark kept file", path, markedError);
                ++kept;
                continue;
            }
            if (const int removed = io.removeFile(path)) return fail("free copied file", path, removed);
        }
        // Deepest directories first. A directory that still holds anything stays.
        for (auto it = plan.items.rbegin(); it != plan.items.rend(); ++it) {
            if (!it->directory) continue;
            const auto path = inherited_detail::join(tree, it->relative);
            const int removed = io.removeDirectory(path);
            if (removed && removed != ENOTEMPTY && removed != EEXIST && removed != ENOENT)
                return fail("remove emptied folder", path, removed);
        }
        const int removed = io.removeDirectory(tree);
        if (removed && removed != ENOTEMPTY && removed != EEXIST)
            return fail("remove emptied folder", tree, removed);
        if (io.inspect(tree).kind != K::Missing) {
            if (!kept) return fail("folder still holds files that were not planned", tree);
            return true; // Owned and reported; records stay with it.
        }
    }
    if (io.inspect(paths.target).kind != K::Link || io.inspect(paths.original()).kind != K::Missing ||
        io.inspect(paths.staged()).kind != K::Missing || io.inspect(paths.retired()).kind != K::Missing)
        return fail("native folder layout could not be confirmed; records kept", paths.target);
    return io.removeRecords(paths, error);
}

} // namespace unbound
