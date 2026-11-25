#include "title_folder_link.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace unbound;
const FolderLinkPaths Paths{"update/content/english/sound/loaded"};

struct Io {
    std::map<std::string, FolderLinkEntry> entries{{Paths.target, FolderLinkEntry::Link}};
    FolderLinkOwnership ownership = FolderLinkOwnership::Missing;
    std::map<std::string, int> inspectErrors;
    std::set<unsigned> failRenames;
    std::vector<std::string> events;
    bool validBase = true, failJournal = false;
    bool failPreflight = false, failCompletion = false;
    int mkdirError = 0, emptyError = 0;
    unsigned renames = 0, journalWrites = 0, baseChecks = 0, preflights = 0, populations = 0, completions = 0;
    std::set<std::string> stagedBanks, liveBanks;
    std::set<std::string> stockBanks{"zmb_common.english.sabl", "spl_frontend.english.sabl"};

    FolderLinkState inspect(const std::string& path) {
        events.push_back("stat " + path);
        if (inspectErrors.count(path)) return {FolderLinkEntry::Error, inspectErrors.at(path)};
        return {entries.count(path) ? entries.at(path) : FolderLinkEntry::Missing, 0};
    }
    FolderLinkOwnership readOwnership(const FolderLinkPaths& paths, std::string& error) {
        assert(paths.target == Paths.target);
        if (ownership == FolderLinkOwnership::Invalid) error = "Invalid owner record was retained";
        return ownership;
    }
    bool writeOwnership(const FolderLinkPaths& paths, std::string& error) {
        assert(paths.ownershipText() == "HQFOLDERLINK1\ntarget=" + Paths.target + "\n");
        assert(ownership == FolderLinkOwnership::Missing);
        ++journalWrites;
        if (failJournal) { error = "Owner record could not be saved"; return false; }
        ownership = FolderLinkOwnership::Valid;
        return true;
    }
    bool validateOriginalContent(std::string& error) {
        ++baseChecks;
        if (!validBase) error = "Original content unavailable; reference retained";
        return validBase;
    }
    bool preflightInheritedDirectory(const FolderLinkPaths&, bool, std::string& error) {
        ++preflights;
        events.push_back("preflight stock");
        if (failPreflight) { error = "Not enough space; original retained"; return false; }
        return true;
    }
    bool populateInheritedDirectory(const FolderLinkPaths&, bool live, std::string& error) {
        ++populations;
        assert(preflights && ownership == FolderLinkOwnership::Valid);
        events.push_back(live ? "repair missing stock" : "verify staged stock");
        if (emptyError) { error = "Stock copy/verification failed"; return false; }
        auto& banks = live ? liveBanks : stagedBanks;
        banks.insert(stockBanks.begin(), stockBanks.end());
        return true;
    }
    bool markInheritedComplete(const FolderLinkPaths&, std::string& error) {
        ++completions;
        for (const auto& name : stockBanks) assert(liveBanks.count(name));
        if (failCompletion) { error = "Completion receipt write failed"; return false; }
        return true;
    }
    int makeDirectory(const std::string& path) {
        assert(path == Paths.staged());
        assert(ownership == FolderLinkOwnership::Valid); // Durable before mutation.
        events.push_back("mkdir " + path);
        if (mkdirError) return mkdirError;
        assert(!entries.count(path));
        entries[path] = FolderLinkEntry::Directory;
        return 0;
    }
    int checkEmptyDirectory(const std::string& path) {
        assert(path == Paths.staged() && entries.at(path) == FolderLinkEntry::Directory);
        return emptyError;
    }
    int rename(const std::string& from, const std::string& to) {
        assert(ownership == FolderLinkOwnership::Valid);
        assert((from == Paths.target && to == Paths.original()) ||
               (from == Paths.staged() && to == Paths.target) ||
               (from == Paths.original() && to == Paths.target));
        events.push_back("rename " + from + " -> " + to);
        ++renames;
        if (failRenames.count(renames)) return EACCES;
        if (!entries.count(from)) return ENOENT;
        if (entries.count(to)) return EEXIST;
        entries[to] = entries.at(from);
        entries.erase(from);
        if (from == Paths.staged()) { liveBanks = stagedBanks; stagedBanks.clear(); }
        return 0;
    }
};

void assertComplete(const Io& io) {
    assert(io.entries.at(Paths.target) == FolderLinkEntry::Directory);
    assert(io.entries.at(Paths.original()) == FolderLinkEntry::Link);
    assert(!io.entries.count(Paths.staged()));
    assert(io.ownership == FolderLinkOwnership::Valid);
    for (const auto& name : io.stockBanks) assert(io.liveBanks.count(name));
}

void initialAndCompleted() {
    Io io;
    std::string error;
    std::vector<std::string> activity;
    const DirectoryActivity progress = [&](const char* operation, const std::string& path) {
        activity.push_back(std::string(operation) + " " + path);
    };
    assert(materializeTitleFolderLink(Paths, io, error, progress));
    assert(error.empty() && io.journalWrites == 1 && io.baseChecks == 1 && io.renames == 2);
    assertComplete(io);
    assert(!activity.empty());
    assert(activity.back() == "stat " + Paths.staged());
    io.validBase = false;
    assert(!materializeTitleFolderLink(Paths, io, error)); // Old empty-directory success is not enough.
    io.validBase = true;
    assert(materializeTitleFolderLink(Paths, io, error));
    assert(io.journalWrites == 1 && io.baseChecks == 3 && io.renames == 2);
    assertComplete(io);
    for (const auto state : {FolderLinkEntry::Missing, FolderLinkEntry::Directory}) {
        io = {};
        io.entries.clear();
        if (state == FolderLinkEntry::Directory) io.entries[Paths.target] = state;
        assert(materializeTitleFolderLink(Paths, io, error));
        assert(io.journalWrites == 0 && io.renames == 0 && io.baseChecks == 0);
    }
}

void faultsBeforeMovingReference() {
    for (unsigned fault = 0; fault < 8; ++fault) {
        Io io;
        if (fault == 0) io.validBase = false;
        if (fault == 1) io.failJournal = true;
        if (fault == 2) io.mkdirError = ENOSPC;
        if (fault == 3) io.emptyError = ENOTEMPTY;
        if (fault == 4) io.failRenames.insert(1);
        if (fault == 5) io.inspectErrors[Paths.target] = EIO;
        if (fault == 6) io.ownership = FolderLinkOwnership::Invalid;
        if (fault == 7) io.failPreflight = true;
        std::string error;
        assert(!materializeTitleFolderLink(Paths, io, error));
        assert(!error.empty());
        assert(io.entries.at(Paths.target) == FolderLinkEntry::Link);
        assert(!io.entries.count(Paths.original()));
        if (fault < 4 || fault > 4) assert(io.renames == 0);
        if (fault == 7) assert(io.journalWrites == 0 && io.populations == 0);
    }
}

void coverageBeforeActivationAndLegacyRepair() {
    Io io;
    io.stagedBanks.insert("spl_frontend.english.sabl");
    std::string error;
    assert(materializeTitleFolderLink(Paths, io, error));
    const auto copied=std::find(io.events.begin(),io.events.end(),"verify staged stock");
    const auto parked=std::find(io.events.begin(),io.events.end(),"rename "+Paths.target+" -> "+Paths.original());
    assert(copied<parked); // Stock coverage is proven while the original reference is still live.
    assertComplete(io);
    io.liveBanks={"zmb_nuked.english.sabl", "newer-payload.sabl"}; // Actual r35/r36 failure shape.
    assert(materializeTitleFolderLink(Paths, io, error));
    assertComplete(io);
    assert(io.liveBanks.count("zmb_nuked.english.sabl")&&io.liveBanks.count("newer-payload.sabl"));
    assert(io.renames==2); // Missing-only legacy repair does not swap/delete the existing live directory.
    io.failCompletion=true;
    assert(!materializeTitleFolderLink(Paths, io, error));
    assertComplete(io); // A failed receipt leaves complete files and original reference for retry.
    io.failCompletion=false;
    assert(materializeTitleFolderLink(Paths, io, error));
    Io tooLong;
    const FolderLinkPaths invalid{"update/content/"+std::string(224,'x')};
    assert(!materializeTitleFolderLink(invalid,tooLong,error));
    assert(tooLong.events.empty()&&tooLong.journalWrites==0&&tooLong.renames==0);
}

void renameFailureAndRecovery() {
    for (const bool failedRollback : {false, true}) {
        Io io;
        io.failRenames.insert(2); // Stage activation fails after reference was parked.
        if (failedRollback) io.failRenames.insert(3);
        std::string error;
        assert(!materializeTitleFolderLink(Paths, io, error));
        assert(io.ownership == FolderLinkOwnership::Valid);
        assert(io.entries.at(Paths.staged()) == FolderLinkEntry::Directory);
        if (failedRollback) {
            assert(!io.entries.count(Paths.target));
            assert(io.entries.at(Paths.original()) == FolderLinkEntry::Link);
            assert(error.find("restore preserved reference") != std::string::npos);
        } else {
            assert(io.entries.at(Paths.target) == FolderLinkEntry::Link);
            assert(!io.entries.count(Paths.original()));
        }
        io.failRenames.clear();
        assert(materializeTitleFolderLink(Paths, io, error));
        assertComplete(io);
    }
    // Every reachable power-cut checkpoint is recoverable from the static
    // ownership record and actual entry types, with no guessed link recreation.
    for (unsigned checkpoint = 0; checkpoint < 4; ++checkpoint) {
        Io io;
        io.ownership = FolderLinkOwnership::Valid;
        if (checkpoint >= 1) io.entries[Paths.staged()] = FolderLinkEntry::Directory;
        if (checkpoint >= 2) {
            io.entries.erase(Paths.target);
            io.entries[Paths.original()] = FolderLinkEntry::Link;
        }
        if (checkpoint == 3) {
            io.entries[Paths.target] = FolderLinkEntry::Directory;
            io.entries.erase(Paths.staged());
        }
        std::string error;
        assert(materializeTitleFolderLink(Paths, io, error));
        assertComplete(io);
        assert(io.journalWrites == 0);
    }
    // Recovery also restores a missing-target gap if no staged folder survived.
    Io io;
    io.ownership = FolderLinkOwnership::Valid;
    io.entries = {{Paths.original(), FolderLinkEntry::Link}};
    std::string error;
    assert(materializeTitleFolderLink(Paths, io, error));
    assertComplete(io);
}

void unownedAndUnexpectedStates() {
    for (unsigned fault = 0; fault < 8; ++fault) {
        Io io;
        if (fault == 0) io.entries[Paths.original()] = FolderLinkEntry::Link;
        if (fault == 1) io.entries[Paths.staged()] = FolderLinkEntry::Directory;
        if (fault >= 2) io.ownership = FolderLinkOwnership::Valid;
        if (fault == 2) io.entries[Paths.original()] = FolderLinkEntry::Other;
        if (fault == 3) io.entries[Paths.staged()] = FolderLinkEntry::Link;
        if (fault == 4) io.entries[Paths.staged()] = FolderLinkEntry::Other;
        if (fault == 5) io.entries[Paths.target] = FolderLinkEntry::Directory;
        if (fault == 6) io.entries[Paths.target] = FolderLinkEntry::Other;
        if (fault == 7) io.entries[Paths.original()] = FolderLinkEntry::Link;
        const auto before = io.entries;
        std::string error;
        assert(!materializeTitleFolderLink(Paths, io, error));
        assert(io.entries == before && io.renames == 0 && io.journalWrites == 0);
        assert(!error.empty());
    }
    Io io;
    io.entries[Paths.target] = FolderLinkEntry::Other;
    std::string error;
    assert(!materializeTitleFolderLink(Paths, io, error));
    assert(io.entries.at(Paths.target) == FolderLinkEntry::Other);
}

void genericContentPrefixPlan() {
    const std::string root = "storage_usb01:/usr/title/0005000e/1010cf00/content";
    std::set<std::string> planned;
    std::string error;
    for (const auto& outside : {root, root + "-other/video", std::string("elsewhere/content/video"),
                               root.substr(0, root.size() - 7) + "code", root.substr(0, root.size() - 7) + "meta"})
        assert(addContentFolderLinkPrefixes(root, outside, planned, error));
    assert(planned.empty()); // No root, code/meta, other title or prefix-lookalike candidate.
    assert(addContentFolderLinkPrefixes(root, root + "/video/intro/cinematics", planned, error));
    assert(addContentFolderLinkPrefixes(root, root + "/english/sound/loaded", planned, error));
    assert(addContentFolderLinkPrefixes(root, root + "/sound/loaded", planned, error));
    const std::set<std::string> expected{root + "/english", root + "/english/sound",
        root + "/english/sound/loaded", root + "/sound", root + "/sound/loaded",
        root + "/video", root + "/video/intro", root + "/video/intro/cinematics"};
    assert(planned == expected);
    assert(addContentFolderLinkPrefixes(root, root + "/video/intro", planned, error));
    assert(addContentFolderLinkPrefixes(root, root + "/video/intro/cinematics", planned, error));
    assert(planned == expected); // Repeated entry/file-parent paths do not repeat filesystem work.
    std::set<std::string> visited;
    for (const auto& path : planned) {
        const auto slash = path.find_last_of('/');
        const auto parent = path.substr(0, slash);
        assert(parent == root || visited.count(parent));
        visited.insert(path); // A nested inherited parent is handled before its child.
    }
    for (const auto& invalid : {root + "/", root + "//video", root + "/video//intro",
                               root + "/video/../code", root + "/./video", root + "/video\\intro",
                               root + "/video:other", root + "/video.unbound-link-original",
                               root + "/video.UnBoUnD-LiNk-new", root + "/video/\nintro",
                               root + "/video/" + std::string(1, '\0') + "intro", std::string{}}) {
        assert(!addContentFolderLinkPrefixes(root, invalid, planned, error));
        assert(!error.empty() && planned == expected); // No partially appended unsafe prefixes.
    }
    assert(!addContentFolderLinkPrefixes("", root + "/video", planned, error));
    assert(!addContentFolderLinkPrefixes(root + "/", root + "/video", planned, error));
    assert(planned == expected);
    std::string deep = root;
    for (unsigned level = 0; level < 20; ++level) deep += "/d" + std::to_string(level);
    std::set<std::string> deepest;
    assert(addContentFolderLinkPrefixes(root, deep, deepest, error));
    const auto before = deepest;
    assert(!addContentFolderLinkPrefixes(root, deep + "/too-deep", deepest, error));
    assert(deepest == before && !error.empty());
    std::set<std::string> maximum;
    for (std::size_t index = 0; index < MaximumContentFolderLinkCandidates; ++index)
        maximum.insert(root + "/existing" + std::to_string(index));
    const auto maximumBefore = maximum;
    assert(!addContentFolderLinkPrefixes(root, root + "/new/child", maximum, error));
    assert(maximum == maximumBefore && !error.empty());
    assert(addContentFolderLinkPrefixes(root, root + "/existing0", maximum, error));
    assert(maximum == maximumBefore); // A repeated path is still allowed at the cap.
}
void relocatedOwnershipRecords() {
    const std::string current="storage_mlc01:/usr/title/0005000e/1010cf00/content/english/sound/loaded";
    const std::string previous="storage_usb01:/usr/title/0005000e/1010cf00/content/english/sound/loaded";
    const auto currentRecord=FolderLinkPaths{current}.ownershipText();
    const auto oldRecord=FolderLinkPaths{previous}.ownershipText();
    assert(inheritedOwnershipTextMatches(currentRecord,current,previous,false));
    assert(inheritedOwnershipTextMatches(oldRecord,current,previous,true));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,previous,false));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,current,true));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,previous+"/child",true));
    assert(!inheritedOwnershipTextMatches(oldRecord+"unexpected=1\n",current,previous,true));
    assert(!inheritedOwnershipTextMatches("",current,previous,true));
    const auto otherSku="storage_usb01:/usr/title/0005000e/1010d000/content/english/sound/loaded";
    assert(!inheritedOwnershipTextMatches(FolderLinkPaths{otherSku}.ownershipText(),current,otherSku,true));
    const auto otherLanguage="storage_usb01:/usr/title/0005000e/1010cf00/content/french/sound/loaded";
    assert(!inheritedOwnershipTextMatches(FolderLinkPaths{otherLanguage}.ownershipText(),current,otherLanguage,true));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,"storage_usb01:/usr/title/../unsafe",true));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,"storage_usb01:\\unsafe",true));
    assert(!inheritedOwnershipTextMatches(oldRecord,current,previous+"\n",true));
}
} // namespace

int main() {
    initialAndCompleted();
    faultsBeforeMovingReference();
    renameFailureAndRecovery();
    unownedAndUnexpectedStates();
    coverageBeforeActivationAndLegacyRepair();
    genericContentPrefixPlan();
    relocatedOwnershipRecords();
    std::cout << "Title folder links: preserved reference, staged rename, ownership, fault rollback and interrupted recovery passed\n";
}
