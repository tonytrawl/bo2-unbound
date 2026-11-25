#include "directory_policy.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace unbound;

struct FakeIo {
    std::map<std::string, DirectoryStatResult> entries;
    std::map<std::string, int> makeErrors;
    std::map<std::string, DirectoryStatResult> afterMake;
    std::map<std::string, DirectoryProbeResult> probeResults;
    std::vector<std::string> calls;
    std::vector<std::string>* timeline = nullptr;

    void record(const std::string& call) {
        calls.push_back(call);
        if (timeline) timeline->push_back("io " + call);
    }

    DirectoryStatResult inspect(const std::string& path) {
        record("stat " + path);
        const auto found = entries.find(path);
        return found == entries.end() ? DirectoryStatResult{ENOENT, 0} : found->second;
    }
    int make(const std::string& path) {
        record("mkdir " + path);
        const int result = makeErrors.count(path) ? makeErrors[path] : 0;
        if (afterMake.count(path)) entries[path] = afterMake[path];
        else if (!result) entries[path] = {0, S_IFDIR | 0777};
        return result;
    }
    DirectoryProbeResult probe(const std::string& path) {
        record("opendir+closedir " + path);
        return probeResults.count(path) ? probeResults[path] : DirectoryProbeResult{ENOTDIR, false};
    }
};

bool prepare(FakeIo& io, std::set<std::string>& cache, DirectoryFailure& error) {
    return prepareDirectoryPrefixes("root/child", {"root", "root/child"}, cache, io, error);
}

void knownAndCache() {
    FakeIo io;
    io.entries["root"] = {0, S_IFDIR | 0755};
    io.entries["root/child"] = {0, S_IFDIR | 0755};
    DirectoryFailure error;
    std::set<std::string> cache;
    assert(prepare(io, cache, error));
    assert(io.calls == std::vector<std::string>({"stat root", "stat root/child"}));
    assert(prepare(io, cache, error) && io.calls.size() == 2);
}

void unknownProbe() {
    FakeIo io;
    io.entries["root"] = {0, S_IFDIR};
    io.entries["root/child"] = {0, 0400};
    io.probeResults["root/child"] = {};
    DirectoryFailure error;
    std::set<std::string> cache;
    assert(prepare(io, cache, error));
    assert(io.calls.back() == "opendir+closedir root/child");
    assert(cache.count("root/child"));

    for (const bool closeFailed : {false, true}) {
        cache.clear();
        io.calls.clear();
        io.probeResults["root/child"] = {EACCES, closeFailed};
        assert(!prepare(io, cache, error));
        assert(!cache.count("root/child") && cache.count("root"));
        assert(error.path == "root/child" && error.error == EACCES);
        assert(error.modeKnown && error.mode == 0400);
        assert(error.operation.find(closeFailed ? "closedir" : "opendir") == 0);
        assert(error.message().find("errno=") != std::string::npos);
        io.probeResults["root/child"] = {};
        assert(prepare(io, cache, error));
    }
}

void typedEntriesCannotBeProbed() {
    for (const auto type : {S_IFREG, S_IFLNK, S_IFCHR, S_IFIFO}) {
        FakeIo io;
        io.entries["root"] = {0, S_IFDIR};
        io.entries["root/child"] = {0, static_cast<std::uint32_t>(type | 0777)};
        // Even a hypothetical successful opendir must not authorize a link.
        io.probeResults["root/child"] = {};
        DirectoryFailure error;
        std::set<std::string> cache;
        assert(!prepare(io, cache, error));
        assert(io.calls == std::vector<std::string>({"stat root", "stat root/child"}));
        assert(error.error == ENOTDIR && !cache.count("root/child"));
        if (type == S_IFLNK) assert(error.message().find("link/reference") != std::string::npos);
        if (type == S_IFREG) assert(error.message().find("regular file") != std::string::npos);
    }
}

void errorsNeverBecomeMissingDirectories() {
    for (const int code : {EACCES, EPERM, EIO, ENOTDIR}) {
        FakeIo io;
        io.entries["root"] = {0, S_IFDIR};
        io.entries["root/child"] = {code, 0};
        DirectoryFailure error;
        std::set<std::string> cache;
        assert(!prepare(io, cache, error));
        assert(io.calls == std::vector<std::string>({"stat root", "stat root/child"}));
        assert(error.error == code && error.path == "root/child");
        assert(!cache.count("root/child"));
    }
}

void missingAndRaces() {
    FakeIo io;
    io.entries["root"] = {0, S_IFDIR};
    DirectoryFailure error;
    std::set<std::string> cache;
    assert(prepare(io, cache, error));
    assert(io.calls.back() == "mkdir root/child" && cache.count("root/child"));

    for (const auto mode : {S_IFDIR, S_IFREG, S_IFLNK, 0}) {
        io = {};
        io.entries["root"] = {0, S_IFDIR};
        io.makeErrors["root/child"] = EEXIST;
        io.afterMake["root/child"] = {0, static_cast<std::uint32_t>(mode)};
        io.probeResults["root/child"] = {};
        cache.clear();
        const bool expected = mode == S_IFDIR || mode == 0;
        assert(prepare(io, cache, error) == expected);
        assert(cache.count("root/child") == static_cast<unsigned>(expected));
    }
    io = {};
    io.entries["root"] = {0, S_IFDIR};
    io.makeErrors["root/child"] = EEXIST;
    io.afterMake["root/child"] = {EACCES, 0};
    cache.clear();
    assert(!prepare(io, cache, error));
    assert(error.error == EACCES && error.operation == "stat after mkdir EEXIST");
    assert(!cache.count("root/child"));

    io = {};
    io.entries["root"] = {0, S_IFDIR};
    io.makeErrors["root/child"] = ENOSPC;
    cache.clear();
    assert(!prepare(io, cache, error));
    assert(error.error == ENOSPC && error.operation == "mkdir");
    assert(!cache.count("root/child"));
}

void rootsAreReadOnlyChecks() {
    FakeIo io;
    DirectoryFailure error;
    std::set<std::string> cache;
    assert(!prepareDirectoryPrefixes("root", {}, cache, io, error));
    assert(io.calls == std::vector<std::string>({"stat root"}));
    assert(error.operation == "stat: mount root");
    assert(!prepareDirectoryPrefixes("", {}, cache, io, error));
    assert(error.error == EINVAL);
}

void activityPrecedesEveryIo() {
    FakeIo io;
    io.entries["root"] = {0, S_IFDIR};
    io.makeErrors["root/child"] = EEXIST;
    io.afterMake["root/child"] = {0, 0400};
    io.probeResults["root/child"] = {};
    DirectoryFailure error;
    std::set<std::string> cache;
    std::vector<std::string> timeline;
    io.timeline = &timeline;
    const DirectoryActivity activity = [&](const char* operation, const std::string& path) {
        timeline.push_back("activity " + std::string(operation) + " " + path);
    };
    assert(prepareDirectoryPrefixes("root/child", {"root", "root/child"}, cache, io, error, activity));
    assert(timeline == std::vector<std::string>({
        "activity stat root", "io stat root",
        "activity stat root/child", "io stat root/child",
        "activity mkdir root/child", "io mkdir root/child",
        "activity stat root/child", "io stat root/child",
        "activity opendir/closedir root/child", "io opendir+closedir root/child"}));
    const auto completed = timeline;
    assert(prepareDirectoryPrefixes("root/child", {"root", "root/child"}, cache, io, error, activity));
    assert(timeline == completed); // A cache hit adds no progress or filesystem work.

    for (const int statError : {EACCES, EIO}) {
        io = {};
        io.entries["root"] = {statError, 0};
        io.timeline = &timeline;
        cache.clear(); timeline.clear();
        assert(!prepareDirectoryPrefixes("root", {}, cache, io, error, activity));
        assert(timeline == std::vector<std::string>({"activity stat root", "io stat root"}));
        assert(error.error == statError);
    }
    for (const auto type : {S_IFREG, S_IFLNK}) {
        io = {};
        io.entries["root"] = {0, S_IFDIR};
        io.entries["root/child"] = {0, static_cast<std::uint32_t>(type)};
        io.probeResults["root/child"] = {};
        io.timeline = &timeline;
        cache.clear(); timeline.clear();
        assert(!prepareDirectoryPrefixes("root/child", {"root", "root/child"}, cache, io, error, activity));
        assert(timeline == std::vector<std::string>({
            "activity stat root", "io stat root", "activity stat root/child", "io stat root/child"}));
        assert(error.error == ENOTDIR); // Typed links/files never announce or attempt an open.
    }
    io = {};
    io.entries["root"] = {0, 0400};
    io.probeResults["root"] = {EACCES, false};
    io.timeline = &timeline;
    cache.clear(); timeline.clear();
    assert(!prepareDirectoryPrefixes("root", {}, cache, io, error, activity));
    assert(timeline == std::vector<std::string>({"activity stat root", "io stat root",
        "activity opendir/closedir root", "io opendir+closedir root"}));
    assert(error.error == EACCES && !cache.count("root"));
}
} // namespace

int main() {
    knownAndCache();
    unknownProbe();
    typedEntriesCannotBeProbed();
    errorsNeverBecomeMissingDirectories();
    missingAndRaces();
    rootsAreReadOnlyChecks();
    activityPrecedesEveryIo();
    std::cout << "Directory policy: typed-link/file rejection, read-only unknown probes, captured failures, creation races and cache tests passed\n";
}
