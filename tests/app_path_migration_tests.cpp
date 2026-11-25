#include "app_path_migration.hpp"

#include <cassert>
#include <set>
#include <utility>

using namespace unbound;

namespace {
struct FakeIo {
    std::set<std::string> paths;
    bool released = false;
    bool refuseRelease = false;
    bool refuseFileRename = false;
    bool refuseMerge = false;
    bool merged = false;

    bool mergeState(const std::string& from, const std::string& to, std::string& error) {
        if (refuseMerge) { error = "State merge failed"; return false; }
        merged = true;
        for (const auto& path : std::set<std::string>(paths)) {
            if (path.starts_with(from + "/") && !path.ends_with(".wuhb"))
                paths.insert(to + path.substr(from.size()));
        }
        return true;
    }

    bool validWuhb(const std::string& path) { return paths.contains(path) && path.ends_with(".wuhb"); }
    bool missing(const std::string& path) { return !paths.contains(path); }
    bool release(std::string& error) {
        if (refuseRelease) { error = "Mount still in use"; return false; }
        released = true;
        return true;
    }
    bool rename(const std::string& from, const std::string& to, std::string& error) {
        assert(released && paths.contains(from) && !paths.contains(to));
        if (refuseFileRename && from.ends_with(".wuhb")) { error = "File rename failed"; return false; }
        const auto snapshot = paths;
        for (const auto& path : snapshot) {
            if (path != from && !path.starts_with(from + "/")) continue;
            paths.erase(path);
            paths.insert(to + path.substr(from.size()));
        }
        return true;
    }
};
}

int main() {
    const std::string oldRoot = config::LegacyHqSdRoot;
    const std::string newRoot = config::PublishedSdRoot;
    const std::string legacy = oldRoot + "/BO2-Unbound-HQ.wuhb";
    const std::string intermediate = newRoot + "/BO2-Unbound-HQ.wuhb";
    const std::string canonical = config::AppWuhbPath;
    const std::string settings = "/settings.ini";
    std::string error;
    const FakeIo original{{oldRoot, legacy, oldRoot + settings}};
    auto io = original;
    assert(migrateHqPath("wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb", io, error) == AppPathMigration::Migrated);
    assert(io.paths == std::set<std::string>({newRoot, canonical, newRoot + settings}));
    io.released = false;
    assert(migrateHqPath(canonical, io, error) == AppPathMigration::Unchanged && !io.released);
    io = FakeIo{{newRoot, intermediate, newRoot + settings}};
    assert(migrateHqPath("sd:/wiiu/apps/Unbound-HQ/BO2-Unbound-HQ.wuhb", io, error) == AppPathMigration::Migrated);
    assert(io.paths.contains(canonical) && io.paths.contains(newRoot + settings));

    io = original;
    io.paths.insert(newRoot);
    io.paths.insert(canonical);
    assert(migrateHqPath(legacy, io, error) == AppPathMigration::Failed && !io.released);
    assert(io.paths.contains(legacy) && io.paths.contains(canonical));

    const std::string renamedLegacy = oldRoot + "/" + config::AppWuhbName;
    io = FakeIo{{oldRoot, renamedLegacy, oldRoot + settings}};
    assert(migrateHqPath(renamedLegacy, io, error) == AppPathMigration::Migrated);
    assert(io.paths == std::set<std::string>({newRoot, canonical, newRoot + settings}));

    const auto mapSelection = newRoot + "/state/maps/selection.txt";
    const FakeIo split{{oldRoot, renamedLegacy, oldRoot + settings, newRoot, mapSelection}};
    io = split;
    assert(migrateHqPath("sd:/wiiu/apps/BO2-Unbound-HQ/Unbound-HQ.wuhb", io, error) == AppPathMigration::Migrated);
    assert(io.merged && !io.paths.contains(renamedLegacy) && io.paths.contains(canonical));
    assert(io.paths.contains(newRoot + settings) && io.paths.contains(oldRoot + settings));
    assert(io.paths.contains(mapSelection));
    io = split;
    io.refuseMerge = true;
    assert(migrateHqPath(renamedLegacy, io, error) == AppPathMigration::Failed && !io.released);
    assert(io.paths == split.paths);
    io = split;
    io.refuseFileRename = true;
    assert(migrateHqPath(renamedLegacy, io, error) == AppPathMigration::Failed);
    assert(io.paths.contains(renamedLegacy) && !io.paths.contains(canonical) && io.paths.contains(mapSelection));
    io.refuseFileRename = false;
    assert(migrateHqPath(renamedLegacy, io, error) == AppPathMigration::Migrated);

    io = original;
    io.refuseRelease = true;
    assert(migrateHqPath(legacy, io, error) == AppPathMigration::Failed && io.paths == original.paths);
    io = original;
    io.refuseFileRename = true;
    assert(migrateHqPath(legacy, io, error) == AppPathMigration::Failed && io.paths == original.paths);
}
