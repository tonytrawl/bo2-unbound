#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace unbound {

enum class InheritedEntryKind { Missing, Directory, Regular, Link, Other, Error };
struct InheritedStat {
    InheritedEntryKind kind = InheritedEntryKind::Missing;
    std::uint64_t bytes = 0;
    int error = 0;
};
enum class InheritedCopyMode { FreshStage, RepairMissing };

struct InheritedLimits {
    unsigned maximumDepth = 20;
    std::size_t maximumEntries = 32768;
    std::uint64_t maximumFileBytes = 2ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumTotalBytes = 32ULL * 1024 * 1024 * 1024;
    std::size_t maximumPathBytes = 639;
};
struct InheritedTreeItem {
    std::string relative;
    std::uint64_t bytes = 0;
    bool directory = false;
    bool copy = false;
    bool mayReplaceOwnedStage = false;
};
struct InheritedTreePlan {
    std::string sourceRoot;
    std::string targetRoot;
    InheritedCopyMode mode = InheritedCopyMode::RepairMissing;
    std::vector<InheritedTreeItem> items;
    std::uint64_t sourceBytes = 0;
    std::uint64_t copyBytes = 0;
    // Additional destination allocation. Interrupted stages may also need a
    // bounded temporary replacement while their old verified file still exists.
    std::uint64_t additionalBytes = 0;
    std::uint64_t scratchBytes = 0;
    unsigned files = 0;
    unsigned directories = 0;
};

namespace inherited_detail {
inline bool add(std::uint64_t& total, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - total) return false;
    total += amount;
    return true;
}
inline std::string lower(std::string value) {
    for (char& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
inline bool component(const std::string& name) {
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos) return false;
    for (const unsigned char ch : name) if (ch < 0x20 || ch == 0x7f) return false;
    const auto folded = lower(name);
    // Stock inheritance must not import HQ's own journals or temporary files.
    if (folded.find(".unbound-link-") != std::string::npos) return false;
    for (const std::string ending : {".unbound-new", ".unbound-old", ".unbound-replacing"})
        if (folded.size() >= ending.size() &&
            folded.compare(folded.size() - ending.size(), ending.size(), ending) == 0) return false;
    return true;
}
inline bool root(const std::string& path) {
    if (path.empty() || path.back() == '/' || path.find('\\') != std::string::npos ||
        path.find("//") != std::string::npos || path.find("/../") != std::string::npos ||
        path.find("/./") != std::string::npos || path.ends_with("/..") || path.ends_with("/.")) return false;
    for (const unsigned char ch : path) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}
inline bool under(const std::string& path, const std::string& root) {
    return path.size() > root.size() && path.rfind(root + "/", 0) == 0;
}
inline std::string join(const std::string& root, const std::string& relative) {
    return relative.empty() ? root : root + "/" + relative;
}
inline bool relative(const std::string& path) {
    if (path.empty()) return true; // The implicit inventory root.
    unsigned depth = 0;
    std::size_t start = 0;
    for (;;) {
        const auto end = path.find('/', start);
        if (++depth > 20 || !component(path.substr(start,
            end == std::string::npos ? std::string::npos : end - start))) return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    }
}
inline std::string failure(const char* action, const std::string& path) {
    return std::string("Inherited content: ") + action + ": " + path;
}
} // namespace inherited_detail

// A private stage may contain only the inventoried source tree. During retry,
// regular temporary/replacement siblings of EXACT planned files are allowed;
// the verified-copy adapter must resolve them before final activation. Unknown
// old files, directories or typed links are retained and rejected, never copied
// into the live game just because this is an HQ-owned staging directory.
template<class Io>
bool validateInheritedStageContents(const InheritedTreePlan& plan, Io& io,
                                     std::string& error, bool finalCoverage) {
    if (plan.mode != InheritedCopyMode::FreshStage) return true;
    const auto root = io.inspect(plan.targetRoot);
    if (root.kind == InheritedEntryKind::Missing && !finalCoverage) return true;
    if (root.kind != InheritedEntryKind::Directory) {
        error = inherited_detail::failure("owned stage is not a real directory", plan.targetRoot);
        return false;
    }
    std::map<std::string, bool> expected;
    for (const auto& item : plan.items) expected.emplace(item.relative, item.directory);
    std::vector<std::string> pending{{}};
    std::size_t visited = 0;
    for (std::size_t position = 0; position < pending.size(); ++position) {
        const auto relative = pending[position];
        std::vector<std::string> names;
        const auto directory = inherited_detail::join(plan.targetRoot, relative);
        if (!io.listNames(directory, names, error)) {
            if (error.empty()) error = inherited_detail::failure("could not inspect owned stage", directory);
            return false;
        }
        std::set<std::string> seen;
        for (const auto& name : names) {
            if (++visited > 3 * 32768 || name.empty() || name.size() > 255 || name == "." || name == ".." ||
                name.find_first_of("/\\:") != std::string::npos || !seen.insert(inherited_detail::lower(name)).second) {
                error = inherited_detail::failure("invalid or excessive owned stage entries", directory);
                return false;
            }
            for (const unsigned char ch : name) if (ch < 0x20 || ch == 0x7f) {
                error = inherited_detail::failure("invalid owned stage name", directory);
                return false;
            }
            const auto child = relative.empty() ? name : relative + "/" + name;
            const auto path = inherited_detail::join(plan.targetRoot, child);
            const auto state = io.inspect(path);
            const auto found = expected.find(child);
            if (found != expected.end()) {
                if (state.kind != (found->second ? InheritedEntryKind::Directory : InheritedEntryKind::Regular)) {
                    error = inherited_detail::failure("owned stage child has an unexpected type", path);
                    return false;
                }
                if (found->second) pending.push_back(child);
                continue;
            }
            bool ownedScratch = false;
            if (!finalCoverage && state.kind == InheritedEntryKind::Regular) {
                for (const std::string suffix : {".unbound-new", ".unbound-replacing"}) {
                    if (child.size() <= suffix.size() || !child.ends_with(suffix)) continue;
                    const auto stock = expected.find(child.substr(0, child.size() - suffix.size()));
                    if (stock != expected.end() && !stock->second) ownedScratch = true;
                }
            }
            if (!ownedScratch) {
                error = inherited_detail::failure("unexpected owned stage entry was retained", path);
                return false;
            }
        }
    }
    return true;
}

// READ-ONLY planning. Caller validates the two title roots and owns the staged
// destination before selecting FreshStage. IO exposes inspect(path) and
// listNames(path, names, error); neither may follow typed links. No output plan
// is committed on error, and no filesystem mutation occurs here.
template<class Io>
bool buildInheritedTreePlan(const std::string& sourceRoot, const std::string& targetRoot,
                             InheritedCopyMode mode, Io& io, InheritedTreePlan& output,
                             std::string& error, const InheritedLimits& limits = {}) {
    error.clear();
    const auto fail = [&](const char* action, const std::string& path) {
        error = inherited_detail::failure(action, path);
        return false;
    };
    if (!inherited_detail::root(sourceRoot) || !inherited_detail::root(targetRoot) ||
        sourceRoot == targetRoot || inherited_detail::under(sourceRoot, targetRoot) ||
        inherited_detail::under(targetRoot, sourceRoot) || !limits.maximumEntries ||
        sourceRoot.size() > limits.maximumPathBytes || targetRoot.size() > limits.maximumPathBytes)
        return fail("invalid or overlapping roots", targetRoot);
    InheritedTreePlan plan;
    plan.sourceRoot = sourceRoot;
    plan.targetRoot = targetRoot;
    plan.mode = mode;
    struct Pending { std::string relative; unsigned depth; };
    std::vector<Pending> pending{{{}, 0}};
    for (std::size_t position = 0; position < pending.size(); ++position) {
        const auto current = pending[position];
        if (current.depth > limits.maximumDepth || plan.items.size() >= limits.maximumEntries)
            return fail("inventory depth or entry limit exceeded", sourceRoot);
        const auto source = inherited_detail::join(sourceRoot, current.relative);
        const auto target = inherited_detail::join(targetRoot, current.relative);
        if (source.size() > limits.maximumPathBytes || target.size() > limits.maximumPathBytes)
            return fail("path length limit exceeded", source);
        const auto stock = io.inspect(source);
        const auto existing = io.inspect(target);
        if (stock.kind != InheritedEntryKind::Directory && stock.kind != InheritedEntryKind::Regular)
            return fail("source is unavailable, a reference, or unsupported", source);
        if (existing.kind == InheritedEntryKind::Error || existing.kind == InheritedEntryKind::Link ||
            existing.kind == InheritedEntryKind::Other)
            return fail("destination is unreadable or has an unsupported type", target);
        const bool directory = stock.kind == InheritedEntryKind::Directory;
        if ((directory && existing.kind == InheritedEntryKind::Regular) ||
            (!directory && existing.kind == InheritedEntryKind::Directory))
            return fail("file/directory type conflict; existing content retained", target);
        if (current.relative.empty() && !directory)
            return fail("source root is not a directory", source);
        InheritedTreeItem item{current.relative, directory ? 0 : stock.bytes, directory, false, false};
        if (directory) {
            ++plan.directories;
            std::vector<std::string> names;
            if (!io.listNames(source, names, error)) {
                if (error.empty()) error = inherited_detail::failure("could not enumerate source", source);
                return false;
            }
            std::sort(names.begin(), names.end());
            std::set<std::string> foldedNames;
            for (const auto& name : names) {
                if (!inherited_detail::component(name) || !foldedNames.insert(inherited_detail::lower(name)).second)
                    return fail("unsafe or colliding source entry", source + "/" + name);
                if (pending.size() >= limits.maximumEntries)
                    return fail("inventory entry limit exceeded", source);
                pending.push_back({current.relative.empty() ? name : current.relative + "/" + name,
                                   current.depth + 1});
            }
        } else {
            ++plan.files;
            if (stock.bytes > limits.maximumFileBytes ||
                !inherited_detail::add(plan.sourceBytes, stock.bytes) || plan.sourceBytes > limits.maximumTotalBytes)
                return fail("stock file or total size limit exceeded", source);
            item.copy = existing.kind == InheritedEntryKind::Missing || mode == InheritedCopyMode::FreshStage;
            item.mayReplaceOwnedStage = mode == InheritedCopyMode::FreshStage;
            if (item.copy) {
                constexpr std::size_t TemporarySuffixBytes = sizeof(".unbound-new") - 1;
                const auto leaf = current.relative.substr(current.relative.find_last_of('/') + 1);
                if (target.size() > limits.maximumPathBytes - std::min(limits.maximumPathBytes, TemporarySuffixBytes) ||
                    leaf.size() > 255 - TemporarySuffixBytes)
                    return fail("verified-copy temporary path exceeds native limits", target);
                if (!inherited_detail::add(plan.copyBytes, stock.bytes))
                    return fail("copy size overflow", source);
                const auto oldBytes = existing.kind == InheritedEntryKind::Regular ? existing.bytes : 0;
                if (stock.bytes > oldBytes && !inherited_detail::add(plan.additionalBytes, stock.bytes - oldBytes))
                    return fail("allocation size overflow", target);
                if (existing.kind == InheritedEntryKind::Regular)
                    plan.scratchBytes = std::max(plan.scratchBytes, stock.bytes);
            }
        }
        plan.items.push_back(std::move(item));
    }
    if (!validateInheritedStageContents(plan, io, error, false)) return false;
    output = std::move(plan);
    return true;
}

// Calculate a conservative peak allocation before any write. Overlapping roots
// must be removed using addInheritedTreePlan below. Scratch is reused file by
// file, so it is a maximum, not a sum. The reserve also covers directory/receipt
// metadata. A known budget never authorizes exceeding the available byte count.
inline bool inheritedRequiredSpace(const std::vector<InheritedTreePlan>& plans,
                                    std::uint64_t reserve, std::uint64_t& required,
                                    std::string& error) {
    error.clear();
    std::uint64_t total = reserve, scratch = 0;
    for (const auto& plan : plans) {
        if (!inherited_detail::add(total, plan.additionalBytes)) {
            error = "Inherited content allocation size overflow.";
            return false;
        }
        scratch = std::max(scratch, plan.scratchBytes);
    }
    if (!inherited_detail::add(total, scratch)) {
        error = "Inherited content temporary size overflow.";
        return false;
    }
    required = total;
    return true;
}

// Parent plans replace equivalent descendants. Disagreeing source mappings or
// modes are rejected, rather than silently copying the same destination twice.
inline bool addInheritedTreePlan(std::vector<InheritedTreePlan>& plans,
                                  InheritedTreePlan candidate, std::string& error) {
    error.clear();
    if (!inherited_detail::root(candidate.sourceRoot) || !inherited_detail::root(candidate.targetRoot)) {
        error = "Inherited content plan has an invalid root.";
        return false;
    }
    for (const auto& plan : plans) {
        if (plan.targetRoot == candidate.targetRoot || inherited_detail::under(candidate.targetRoot, plan.targetRoot)) {
            const auto suffix = candidate.targetRoot.substr(plan.targetRoot.size());
            if (plan.mode != candidate.mode || candidate.sourceRoot != plan.sourceRoot + suffix) {
                error = "Inherited content plans have conflicting source roots.";
                return false;
            }
            return true;
        }
        if (inherited_detail::under(plan.targetRoot, candidate.targetRoot)) {
            const auto suffix = plan.targetRoot.substr(candidate.targetRoot.size());
            if (plan.mode != candidate.mode || plan.sourceRoot != candidate.sourceRoot + suffix) {
                error = "Inherited content plans have conflicting source roots.";
                return false;
            }
        }
    }
    plans.erase(std::remove_if(plans.begin(), plans.end(), [&](const auto& plan) {
        return inherited_detail::under(plan.targetRoot, candidate.targetRoot);
    }), plans.end());
    plans.push_back(std::move(candidate));
    return true;
}

// Caller preflights the complete plan's space budget before this mutating step.
// IO additionally exposes makeDirectory(path,error) and copyInheritedFile(from,
// to,expectedBytes,mayReplaceOwnedStage,error). The copy adapter must verify exact
// length and destination digest and commit atomically; on failure it must leave
// the live target absent/unchanged. FreshStage regular files are always sent to
// that adapter for verification or rewrite, never trusted merely for existing.
template<class Io>
bool executeInheritedTreePlan(const InheritedTreePlan& plan, Io& io, std::string& error) {
    error.clear();
    const auto fail = [&](const char* action, const std::string& path) {
        error = inherited_detail::failure(action, path);
        return false;
    };
    if (plan.items.empty() || !plan.items.front().relative.empty() || !plan.items.front().directory)
        return fail("empty or invalid stock plan", plan.sourceRoot);
    if (!inherited_detail::root(plan.sourceRoot) || !inherited_detail::root(plan.targetRoot) ||
        plan.sourceRoot == plan.targetRoot || inherited_detail::under(plan.sourceRoot, plan.targetRoot) ||
        inherited_detail::under(plan.targetRoot, plan.sourceRoot))
        return fail("invalid or overlapping stock plan roots", plan.sourceRoot);
    // The production planner already enforces these properties. Recheck the
    // lexical boundary before the first write too, so a later receipt/cache
    // adapter cannot turn a malformed reconstructed plan into path traversal.
    std::set<std::string> seen;
    for (const auto& item : plan.items) {
        if (!inherited_detail::relative(item.relative) || !seen.insert(item.relative).second ||
            inherited_detail::join(plan.sourceRoot, item.relative).size() > 639 ||
            inherited_detail::join(plan.targetRoot, item.relative).size() > 639 ||
            (plan.mode == InheritedCopyMode::RepairMissing && item.mayReplaceOwnedStage))
            return fail("unsafe or inconsistent stock plan entry", item.relative);
        if (item.copy && !item.directory &&
            (inherited_detail::join(plan.targetRoot, item.relative).size() > 639 - (sizeof(".unbound-new") - 1) ||
             item.relative.substr(item.relative.find_last_of('/') + 1).size() > 255 - (sizeof(".unbound-new") - 1)))
            return fail("stock-copy temporary path exceeds native limits", item.relative);
    }
    if (!validateInheritedStageContents(plan, io, error, false)) return false;
    for (const auto& item : plan.items) {
        const auto source = inherited_detail::join(plan.sourceRoot, item.relative);
        const auto target = inherited_detail::join(plan.targetRoot, item.relative);
        const auto stock = io.inspect(source);
        if (stock.kind != (item.directory ? InheritedEntryKind::Directory : InheritedEntryKind::Regular) ||
            (!item.directory && stock.bytes != item.bytes))
            return fail("source changed or disappeared; retry before playing", source);
        auto existing = io.inspect(target);
        if (item.directory) {
            if (existing.kind == InheritedEntryKind::Missing) {
                if (!io.makeDirectory(target, error)) return false;
                existing = io.inspect(target);
            }
            if (existing.kind != InheritedEntryKind::Directory)
                return fail("required destination is not a directory", target);
        } else if (existing.kind == InheritedEntryKind::Regular && plan.mode == InheritedCopyMode::RepairMissing) {
            // Existing live files belong to the installed payload/user. Their
            // bytes may intentionally differ from retail; never downgrade them.
            continue;
        } else {
            if (existing.kind != InheritedEntryKind::Missing && existing.kind != InheritedEntryKind::Regular)
                return fail("required file has an unsupported destination type", target);
            if (!item.copy) return fail("destination changed after space preflight", target);
            if (!io.copyInheritedFile(source, target, item.bytes, item.mayReplaceOwnedStage, error)) return false;
            const auto copied = io.inspect(target);
            if (copied.kind != InheritedEntryKind::Regular || copied.bytes != item.bytes)
                return fail("verified stock copy did not reach its final path", target);
        }
    }
    // Completeness must be checked before a staged directory is activated or a
    // migration completion receipt may be committed. This catches a lost drive
    // or concurrent deletion after an earlier successful individual copy.
    for (const auto& item : plan.items) {
        const auto target = inherited_detail::join(plan.targetRoot, item.relative);
        const auto state = io.inspect(target);
        if (state.kind != (item.directory ? InheritedEntryKind::Directory : InheritedEntryKind::Regular))
            return fail("inherited child coverage is incomplete", target);
        if (!item.directory && plan.mode == InheritedCopyMode::FreshStage && state.bytes != item.bytes)
            return fail("staged inherited file length changed", target);
    }
    return validateInheritedStageContents(plan, io, error, true);
}

template<class Io>
bool executeInheritedTreePlans(const std::vector<InheritedTreePlan>& plans,
                                std::uint64_t availableBytes, std::uint64_t reserve,
                                Io& io, std::string& error) {
    std::uint64_t required = 0;
    if (!inheritedRequiredSpace(plans, reserve, required, error)) return false;
    if (required > availableBytes) {
        error = "Not enough space to preserve inherited content. Required additional bytes: " +
                std::to_string(required) + "; available bytes: " + std::to_string(availableBytes) + ".";
        return false;
    }
    for (const auto& plan : plans)
        if (!executeInheritedTreePlan(plan, io, error)) return false;
    return true;
}

} // namespace unbound
