#include "native_link.hpp"
#include "retained_reference_restore.hpp"

#include <cassert>
#include <cerrno>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace unbound;
using K = InheritedEntryKind;

const std::string Update = "usb:/usr/title/0005000e/10113400/content";
const std::string Base = "usb:/usr/title/00050000/10113400/content";
const FolderLinkPaths Paths{Update + "/video"};
const std::string Stock = Base + "/video";
const std::string Localized = Update + "/english/video";

struct Node { K kind = K::Missing; std::uint64_t bytes = 0; std::string data; };

// Native references are modelled as opaque Link entries. Only rename moves them.
struct Fs {
    std::map<std::string, Node> nodes;
    std::set<std::string> records{Paths.journal(), Paths.complete()};
    std::set<std::string> failRename, unreadable;
    std::vector<std::string> events;

    void dir(const std::string& path) { nodes[path] = {K::Directory, 0, {}}; }
    void file(const std::string& path, const std::string& data) { nodes[path] = {K::Regular, data.size(), data}; }
    K kind(const std::string& path) const { return nodes.count(path) ? nodes.at(path).kind : K::Missing; }

    InheritedStat inspect(const std::string& path) {
        if (records.count(path)) return {K::Regular, 10, 0};
        if (!nodes.count(path)) return {};
        return {nodes.at(path).kind, nodes.at(path).bytes, 0};
    }
    bool listNames(const std::string& path, std::vector<std::string>& names, std::string& error) {
        names.clear();
        if (kind(path) != K::Directory) { error = "not a directory: " + path; return false; }
        for (const auto& [name, node] : nodes) {
            if (name.rfind(path + "/", 0) != 0) continue;
            const auto rest = name.substr(path.size() + 1);
            if (rest.find('/') == std::string::npos) names.push_back(rest);
        }
        return true;
    }
    bool recordsPresent(const FolderLinkPaths&) { return !records.empty(); }
    int rename(const std::string& from, const std::string& to) {
        events.push_back("rename " + from + " -> " + to);
        if (failRename.count(from)) return EIO;
        if (!nodes.count(from) || nodes.count(to)) return EEXIST;
        std::map<std::string, Node> moved;
        for (auto it = nodes.begin(); it != nodes.end();) {
            if (it->first == from || it->first.rfind(from + "/", 0) == 0) {
                moved[to + it->first.substr(from.size())] = it->second;
                it = nodes.erase(it);
            } else ++it;
        }
        nodes.insert(moved.begin(), moved.end());
        return 0;
    }
    int removeFile(const std::string& path) {
        events.push_back("remove " + path);
        assert(kind(path) == K::Regular);
        nodes.erase(path);
        return 0;
    }
    int removeDirectory(const std::string& path) {
        if (kind(path) != K::Directory) return ENOENT;
        for (const auto& item : nodes) if (item.first.rfind(path + "/", 0) == 0) return ENOTEMPTY;
        events.push_back("rmdir " + path);
        nodes.erase(path);
        return 0;
    }
    bool prepareDirectory(const std::string& path, std::string& error) {
        std::string current = Update;
        assert(path.rfind(Update + "/", 0) == 0);
        std::size_t start = Update.size() + 1;
        for (;;) {
            const auto end = path.find('/', start);
            current = path.substr(0, end);
            if (kind(current) == K::Link || kind(current) == K::Regular) { error = "linked parent " + current; return false; }
            if (kind(current) == K::Missing) dir(current);
            if (end == std::string::npos) return true;
            start = end + 1;
        }
    }
    bool sameContents(const std::string& path, const std::string& other, std::uint64_t, bool& same, std::string& error) {
        if (unreadable.count(path)) { error = "read failed " + path; return false; }
        same = nodes.at(path).data == nodes.at(other).data;
        return true;
    }
    bool removeRecords(const FolderLinkPaths&, std::string&) { records.clear(); events.push_back("records removed"); return true; }
};

Fs converted() {
    Fs fs;
    fs.dir(Update); fs.dir(Update + "/english"); fs.dir(Base); fs.dir(Stock);
    fs.file(Stock + "/intro.bik", "INTRO");
    fs.file(Stock + "/credits.bik", "CREDITS!");
    fs.dir(Stock + "/sub"); fs.file(Stock + "/sub/menu.bik", "MENU");
    fs.dir(Paths.target);
    fs.file(Paths.target + "/intro.bik", "INTRO");
    fs.file(Paths.target + "/credits.bik", "CREDITS!");
    fs.dir(Paths.target + "/sub"); fs.file(Paths.target + "/sub/menu.bik", "MENU");
    fs.file(Paths.target + "/zm_highrise_load.bik", "UNBOUND MOVIE");
    fs.nodes[Paths.original()] = {K::Link, 0, {}};
    return fs;
}

void assertNative(const Fs& fs) {
    assert(fs.kind(Paths.target) == K::Link);
    assert(fs.kind(Paths.original()) == K::Missing && fs.kind(Paths.retired()) == K::Missing &&
           fs.kind(Paths.staged()) == K::Missing);
    assert(fs.records.empty());
    for (const auto& item : fs.nodes) assert(item.first.rfind(Paths.target + "/", 0) != 0);
    // BASE is never written.
    assert(fs.nodes.at(Stock + "/intro.bik").data == "INTRO" && fs.nodes.at(Stock + "/sub/menu.bik").data == "MENU");
}

void r37CopyIsReplacedByReference() {
    auto fs = converted();
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.state == RetainedReferenceState::Converted && plan.needed() && plan.blocked.empty());
    assert(plan.reclaimBytes == 5 + 8 + 4 && plan.relocateBytes == 13 && plan.kept == 0);
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
    assert(fs.nodes.at(Localized + "/zm_highrise_load.bik").data == "UNBOUND MOVIE");
    // The payload moves BEFORE the converted folder is touched.
    std::size_t moved = 0, aside = 0;
    for (std::size_t i = 0; i < fs.events.size(); ++i) {
        if (fs.events[i].find("zm_highrise_load.bik -> " + Localized) != std::string::npos) moved = i + 1;
        if (fs.events[i] == "rename " + Paths.target + " -> " + Paths.retired()) aside = i + 1;
    }
    assert(moved && aside && moved < aside);
    RetainedReferencePlan after;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, after, error));
    assert(after.state == RetainedReferenceState::Clean && !after.needed());
}

void r36EmptyConversionNoLongerHidesStockMovies() {
    Fs fs = converted();
    for (const auto& name : {"/intro.bik", "/credits.bik", "/sub/menu.bik"}) fs.nodes.erase(Paths.target + name);
    fs.nodes.erase(Paths.target + "/sub");
    fs.records = {Paths.journal()}; // r35/r36 never wrote a completion receipt
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error) && plan.needed());
    assert(plan.reclaimBytes == 0 && plan.relocateBytes == 13);
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
    assert(fs.kind(Localized + "/zm_highrise_load.bik") == K::Regular);
}

void modifiedStockNamedFileIsNeverDeleted() {
    auto fs = converted();
    fs.file(Paths.target + "/intro.bik", "MODDED"); // differs in size: relocated, still in effect
    fs.file(Paths.target + "/credits.bik", "CREDITZ!"); // same size, different bytes
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
    assert(fs.nodes.at(Localized + "/intro.bik").data == "MODDED");
    assert(fs.nodes.at(Localized + "/credits.bik").data == "CREDITZ!");
}

void differingFileWithOccupiedDestinationIsKept() {
    auto fs = converted();
    fs.file(Paths.target + "/credits.bik", "CREDITZ!");
    fs.dir(Localized); fs.file(Localized + "/credits.bik", "OTHER!!!");
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assert(fs.kind(Paths.target) == K::Link);
    assert(fs.nodes.at(Paths.retired() + "/credits.bik.unbound-kept").data == "CREDITZ!");
    assert(fs.nodes.at(Localized + "/credits.bik").data == "OTHER!!!");
    assert(!fs.records.empty()); // Owner record stays with the retained folder.
    RetainedReferencePlan after;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, after, error));
    assert(after.state == RetainedReferenceState::Restored && after.kept == 1 && !after.needed());
}

void interruptedAfterSetAsideRestoresReference() {
    auto fs = converted();
    fs.rename(Paths.target, Paths.retired());
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.state == RetainedReferenceState::ReferenceAside && plan.needed());
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
    assert(fs.kind(Localized + "/zm_highrise_load.bik") == K::Regular);
}

void failedReferenceRestoreReactivatesFolder() {
    auto fs = converted();
    fs.failRename.insert(Paths.original());
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(!executeRetainedReferenceRestore(Paths, plan, fs, error));
    assert(error.find("restore original folder reference") != std::string::npos);
    assert(fs.kind(Paths.target) == K::Directory && fs.kind(Paths.original()) == K::Link);
    assert(fs.kind(Paths.retired()) == K::Missing && !fs.records.empty());
    assert(fs.nodes.at(Paths.target + "/intro.bik").data == "INTRO");
}

void noLanguageRouteLeavesWorkingConversion() {
    auto fs = converted();
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, "", fs, plan, error));
    assert(!plan.blocked.empty() && !plan.needed());
    const auto before = fs.nodes;
    assert(!executeRetainedReferenceRestore(Paths, plan, fs, error));
    assert(fs.nodes.size() == before.size() && fs.kind(Paths.target) == K::Directory);
}

void linkedLanguageParentFailsBeforeAnyRename() {
    auto fs = converted();
    fs.nodes[Update + "/english"] = {K::Link, 0, {}};
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(!executeRetainedReferenceRestore(Paths, plan, fs, error));
    assert(error.find("linked parent") != std::string::npos);
    assert(fs.kind(Paths.target) == K::Directory && fs.kind(Paths.original()) == K::Link);
    for (const auto& event : fs.events) assert(event.find("rename") == std::string::npos);
}

void unreadableCopyIsKeptAndRetried() {
    auto fs = converted();
    fs.unreadable.insert(Paths.retired() + "/credits.bik");
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(!executeRetainedReferenceRestore(Paths, plan, fs, error));
    assert(fs.kind(Paths.target) == K::Link && fs.kind(Paths.retired() + "/credits.bik") == K::Regular);
    fs.unreadable.clear();
    RetainedReferencePlan retry;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, retry, error));
    assert(retry.state == RetainedReferenceState::Restored && retry.needed());
    assert(executeRetainedReferenceRestore(Paths, retry, fs, error));
    assertNative(fs);
}

void abandonedR37StageIsCleared() {
    Fs fs = converted();
    for (auto it = fs.nodes.begin(); it != fs.nodes.end();)
        it = it->first.rfind(Paths.target, 0) == 0 ? fs.nodes.erase(it) : std::next(it);
    fs.nodes.erase(Paths.original());
    fs.nodes[Paths.target] = {K::Link, 0, {}};
    fs.dir(Paths.staged()); fs.file(Paths.staged() + "/intro.bik", "INTRO");
    fs.file(Paths.staged() + "/credits.bik.unbound-new", "CRE");
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.state == RetainedReferenceState::StageLeft && plan.relocationRoot.empty());
    assert(plan.reclaimBytes == 5 && plan.kept == 1);
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    // HQ's scratch file is reported, never silently deleted.
    assert(fs.kind(Paths.staged() + "/intro.bik") == K::Missing && fs.kind(Paths.staged()) == K::Directory);
    assert(fs.kind(Paths.target) == K::Link);
}

void stageSetAsideGetsReferenceBack() {
    Fs fs = converted();
    for (auto it = fs.nodes.begin(); it != fs.nodes.end();)
        it = it->first.rfind(Paths.target, 0) == 0 ? fs.nodes.erase(it) : std::next(it);
    fs.nodes[Paths.original()] = {K::Link, 0, {}};
    fs.dir(Paths.staged()); fs.file(Paths.staged() + "/intro.bik", "INTRO");
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.state == RetainedReferenceState::StageAside && plan.needed());
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
}

void ambiguousStateFailsClosed() {
    auto fs = converted();
    fs.nodes.erase(Paths.original());
    RetainedReferencePlan plan;
    std::string error;
    assert(!planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(error.find("ambiguous") != std::string::npos);
    auto both = converted();
    both.dir(Paths.retired());
    assert(!planRetainedReferenceRestore(Paths, Stock, Localized, both, plan, error));
}

void duplicateOfInstalledMovieIsFreed() {
    auto fs = converted();
    fs.dir(Localized); fs.file(Localized + "/zm_highrise_load.bik", "UNBOUND MOVIE");
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.relocateBytes == 0 && plan.reclaimBytes == 17 + 13);
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error));
    assertNative(fs);
    assert(fs.nodes.at(Localized + "/zm_highrise_load.bik").data == "UNBOUND MOVIE");
}

void finishedLayoutOnlyClearsRecords() {
    Fs fs;
    fs.dir(Update); fs.dir(Base); fs.dir(Stock);
    fs.nodes[Paths.target] = {K::Link, 0, {}};
    RetainedReferencePlan plan;
    std::string error;
    assert(planRetainedReferenceRestore(Paths, Stock, Localized, fs, plan, error));
    assert(plan.state == RetainedReferenceState::Clean && plan.needed());
    assert(executeRetainedReferenceRestore(Paths, plan, fs, error) && fs.records.empty());
}

void nativeLinkStaysGated() {
    NativeLinkCandidate candidate{Update + "/sound/loaded", "zmb_common.english.sabl", true,
                                  {K::Directory, 0, 0}, {K::Missing, 0, 0}, {K::Regular, 83968, 0}};
    assert(nativeLinkRefusal(candidate) == NativeLinkRefusal::Unverified);
    assert(nativeLinkRefusal(candidate, true) == NativeLinkRefusal::None);
    auto copy = candidate; copy.existing = {K::Regular, 83968, 0};
    assert(nativeLinkRefusal(copy, true) == NativeLinkRefusal::Occupied); // r37-r41 verified copy
    auto unowned = candidate; unowned.parentOwned = false;
    assert(nativeLinkRefusal(unowned, true) == NativeLinkRefusal::NotOwned);
    auto viaLink = candidate; viaLink.parent = {K::Link, 0, 0};
    assert(nativeLinkRefusal(viaLink, true) == NativeLinkRefusal::NotOwned);
    auto added = candidate; added.original = {};
    assert(nativeLinkRefusal(added, true) == NativeLinkRefusal::NoOriginal);
    auto retained = candidate; retained.name = "loaded.unbound-link-original";
    assert(nativeLinkRefusal(retained, true) == NativeLinkRefusal::LegacyReference);
    auto unsafe = candidate; unsafe.name = "../escape";
    assert(nativeLinkRefusal(unsafe, true) == NativeLinkRefusal::UnsafePath);
    static_assert(!NativeLinkArgumentsVerified);
    static_assert(NativeLinkCommand == 0x75);
}
} // namespace

int main() {
    r37CopyIsReplacedByReference();
    r36EmptyConversionNoLongerHidesStockMovies();
    modifiedStockNamedFileIsNeverDeleted();
    differingFileWithOccupiedDestinationIsKept();
    interruptedAfterSetAsideRestoresReference();
    failedReferenceRestoreReactivatesFolder();
    noLanguageRouteLeavesWorkingConversion();
    linkedLanguageParentFailsBeforeAnyRename();
    unreadableCopyIsKeptAndRetried();
    abandonedR37StageIsCleared();
    stageSetAsideGetsReferenceBack();
    ambiguousStateFailsClosed();
    duplicateOfInstalledMovieIsFreed();
    finishedLayoutOnlyClearsRecords();
    nativeLinkStaysGated();
    std::cout << "retained reference restore tests passed\n";
}
