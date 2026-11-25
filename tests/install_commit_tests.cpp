#include "install_commit.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using unbound::InstallPathState;
const std::string Target = "/content/map.ff";
const std::string Temporary = Target + ".unbound-new";
const std::string Previous = Target + ".unbound-replacing";

struct FakeFileOps {
    struct Entry { InstallPathState state; std::string content; };
    std::map<std::string, Entry> entries;
    std::set<std::string> inspectFailures;
    std::set<unsigned> renameFailures;
    std::set<std::string> removeFailures;
    std::vector<std::string> mutations;
    unsigned renameCalls = 0;

    InstallPathState inspect(const std::string& path) const {
        if (inspectFailures.count(path)) return InstallPathState::Error;
        const auto entry = entries.find(path);
        return entry == entries.end() ? InstallPathState::Missing : entry->second.state;
    }
    bool renameFile(const std::string& source, const std::string& destination) {
        mutations.push_back("rename " + source + " -> " + destination);
        if (renameFailures.count(++renameCalls) || !entries.count(source) || entries.count(destination))
            return false; // Match a filesystem that does not overwrite on rename.
        entries[destination] = entries.at(source);
        entries.erase(source);
        return true;
    }
    bool removeFile(const std::string& path) {
        mutations.push_back("remove " + path);
        if (removeFailures.count(path) || inspect(path) != InstallPathState::RegularFile) return false;
        entries.erase(path);
        return true;
    }
    void regular(const std::string& path, const std::string& content) {
        entries[path] = {InstallPathState::RegularFile, content};
    }
    void expect(const std::string& path, const std::string& content) const {
        assert(inspect(path) == InstallPathState::RegularFile);
        assert(entries.at(path).content == content);
    }
};

FakeFileOps existing() {
    FakeFileOps files;
    files.regular(Target, "original");
    files.regular(Temporary, "verified replacement");
    return files;
}

void successfulCommits() {
    std::string error;
    auto files = existing();
    assert(unbound::commitInstallReplacement(Temporary, Target, error, files));
    files.expect(Target, "verified replacement");
    assert(files.inspect(Previous) == InstallPathState::Missing);
    assert(files.inspect(Temporary) == InstallPathState::Missing);
    assert(files.renameCalls == 2);
    assert(files.mutations == std::vector<std::string>({
        "rename " + Target + " -> " + Previous,
        "rename " + Temporary + " -> " + Target,
        "remove " + Previous}));

    FakeFileOps firstInstall;
    firstInstall.regular(Temporary, "first install");
    assert(unbound::commitInstallReplacement(Temporary, Target, error, firstInstall));
    firstInstall.expect(Target, "first install");
    assert(firstInstall.renameCalls == 1 && firstInstall.mutations.size() == 1);
}

void failedCommits() {
    std::string error;
    auto files = existing();
    files.renameFailures = {1};
    assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
    files.expect(Target, "original");
    files.expect(Temporary, "verified replacement");
    assert(files.inspect(Previous) == InstallPathState::Missing);

    files = existing();
    files.renameFailures = {2};
    assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
    files.expect(Target, "original");
    files.expect(Temporary, "verified replacement");
    assert(files.inspect(Previous) == InstallPathState::Missing);
    assert(files.renameCalls == 3);

    files = existing();
    files.renameFailures = {2, 3};
    assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
    assert(files.inspect(Target) == InstallPathState::Missing);
    files.expect(Previous, "original");
    files.expect(Temporary, "verified replacement");
    assert(error.find(Previous) != std::string::npos);
    files.renameFailures.clear();
    assert(unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Target, "original");
    assert(unbound::commitInstallReplacement(Temporary, Target, error, files));
    files.expect(Target, "verified replacement");

    files = existing();
    files.removeFailures.insert(Previous);
    assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
    files.expect(Target, "verified replacement");
    files.expect(Previous, "original");
    files.removeFailures.clear();
    assert(unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Target, "verified replacement");
    assert(files.inspect(Previous) == InstallPathState::Missing);

    FakeFileOps absentTarget;
    absentTarget.regular(Temporary, "first install");
    absentTarget.renameFailures = {1};
    assert(!unbound::commitInstallReplacement(Temporary, Target, error, absentTarget));
    assert(absentTarget.inspect(Target) == InstallPathState::Missing);
    absentTarget.expect(Temporary, "first install");
}

void interruptedStates() {
    std::string error;
    FakeFileOps files;
    files.regular(Previous, "original");
    files.regular(Temporary, "possibly partial bytes");
    assert(unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Target, "original");
    files.expect(Temporary, "possibly partial bytes"); // Caller must re-extract/verify.
    assert(files.inspect(Previous) == InstallPathState::Missing);

    files = FakeFileOps{};
    files.regular(Target, "committed new file");
    files.regular(Previous, "original");
    files.regular(Target + ".unbound-old", "legacy data must not be removed");
    assert(unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Target, "committed new file");
    files.expect(Target + ".unbound-old", "legacy data must not be removed");
    assert(files.inspect(Previous) == InstallPathState::Missing);

    files = FakeFileOps{};
    files.regular(Previous, "original");
    files.renameFailures = {1};
    assert(!unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Previous, "original");
    assert(error.find(Previous) != std::string::npos);

    files = FakeFileOps{};
    files.regular(Target, "committed");
    files.regular(Previous, "original");
    files.removeFailures.insert(Previous);
    assert(!unbound::recoverInstallReplacement(Target, error, files));
    files.expect(Target, "committed");
    files.expect(Previous, "original");
}

void unsafePaths() {
    std::string error;
    for (const auto& path : {Target, Previous, Temporary}) {
        auto files = existing();
        files.entries[path] = {InstallPathState::Other, "directory or special file"};
        assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
        assert(files.mutations.empty());
    }
    for (const auto& path : {Target, Previous, Temporary}) {
        auto files = existing();
        files.inspectFailures.insert(path);
        assert(!unbound::commitInstallReplacement(Temporary, Target, error, files));
        assert(files.mutations.empty());
        if (path != Target) files.expect(Target, "original");
    }
    auto files = existing();
    assert(!unbound::commitInstallReplacement(Target, Target, error, files));
    assert(!unbound::commitInstallReplacement(Previous, Target, error, files));
    assert(files.mutations.empty());

    using unbound::reservedInstallPayloadPath;
    assert(reservedInstallPayloadPath("update/content/english/sound/loaded.unbound-link-original"));
    assert(reservedInstallPayloadPath("update/content/english/sound/loaded.UNBOUND-LINK-state/child"));
    assert(reservedInstallPayloadPath("update/content/english/sound/loaded.unbound-link-state.unbound-new"));
    for (const std::string path : {"update/content/update.txt", "UPDATE/CONTENT/UPDATE.TXT",
                                  "update/content/update.txt/", "UPDATE/CONTENT/UPDATE.TXT/child",
                                  "update/code/game.rpl.unbound-new", "update/code/X.UNBOUND-OLD",
                                  "update/content/maps.unbound-replacing/map.ff",
                                  "workshop/item/test.UNBOUND-NEW/config.txt"})
        assert(reservedInstallPayloadPath(path));
    for (const std::string path : {"update/content/map.ff", "update/code/game.rpl",
                                  "update/content/mods/item/update.txt", "update/content/updated.txt",
                                  "update/content/test.unbound-new-extra", "workshop/item/modload.txt"})
        assert(!reservedInstallPayloadPath(path));
}

void markerLastAndRetry() {
    std::string error;
    auto files = existing();
    const std::string marker = "/content/update.txt";
    files.regular(marker, "0.0.05");
    const std::string second = "/code/game.rpl";
    files.regular(second, "old executable");
    files.regular(second + ".unbound-new", "new executable");
    assert(unbound::commitInstallReplacement(Temporary, Target, error, files));
    // Fail in a later entry: successfully replaced earlier files remain new,
    // but the installed version is unchanged until the whole package succeeds.
    files.renameFailures.insert(4);
    assert(!unbound::commitInstallReplacement(second + ".unbound-new", second, error, files));
    files.expect(Target, "verified replacement");
    files.expect(second, "old executable");
    files.expect(marker, "0.0.05");
    files.renameFailures.clear();
    files.regular(Temporary, "verified replacement");
    assert(unbound::commitInstallReplacement(Temporary, Target, error, files));
    assert(unbound::commitInstallReplacement(second + ".unbound-new", second, error, files));
    files.regular(marker, "0.0.05P2.1"); // Package owner performs marker-last commit.
    files.expect(second, "new executable");
    files.expect(marker, "0.0.05P2.1");
    assert(files.inspect(Previous) == InstallPathState::Missing);
}

void realFileRoundtrip() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("unbound-commit-" + std::to_string(reinterpret_cast<std::uintptr_t>(&Target)));
    assert(std::filesystem::create_directory(directory));
    const auto target = (directory / "map.ff").string();
    const auto temporary = target + ".unbound-new";
    auto write = [](const std::string& path, const std::string& content) {
        std::ofstream output(path, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        assert(output.good());
    };
    auto read = [](const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const std::string oldBytes("old\0file", 8);
    const std::string newBytes("new\0file\xff", 9);
    write(target, oldBytes);
    write(temporary, newBytes);
    std::string error;
    assert(unbound::recoverInstallReplacement(target, error));
    assert(unbound::commitInstallReplacement(temporary, target, error));
    assert(read(target) == newBytes);
    assert(!std::filesystem::exists(temporary));
    assert(!std::filesystem::exists(target + ".unbound-replacing"));

    assert(std::rename(target.c_str(), (target + ".unbound-replacing").c_str()) == 0);
    assert(unbound::recoverInstallReplacement(target, error));
    assert(read(target) == newBytes);
    assert(std::filesystem::remove(target));
    assert(std::filesystem::remove(directory));
}

void realPackageReplay() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("unbound-replay-" + std::to_string(reinterpret_cast<std::uintptr_t>(&Temporary)));
    assert(std::filesystem::create_directory(directory));
    const auto first = (directory / "map.ff").string();
    const auto second = (directory / "game.rpl").string();
    const auto marker = (directory / "update.txt").string();
    auto write = [](const std::string& path, const std::string& content) {
        std::ofstream output(path, std::ios::binary);
        output << content;
        output.close();
        assert(output.good());
    };
    auto read = [](const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    struct FaultyStdio : unbound::StdioInstallFileOps {
        std::string rejectedSource;
        bool renameFile(const std::string& source, const std::string& target) const {
            if (source == rejectedSource) return false;
            return StdioInstallFileOps::renameFile(source, target);
        }
    } files;
    files.rejectedSource = second + ".unbound-new";
    write(first, "old map");
    write(second, "old executable");
    write(marker, "0.0.05\n");
    write(first + ".unbound-new", "new map");
    write(second + ".unbound-new", "new executable");
    std::string error;
    auto package = [&] {
        if (!unbound::commitInstallReplacement(first + ".unbound-new", first, error, files) ||
            !unbound::commitInstallReplacement(second + ".unbound-new", second, error, files))
            return false;
        write(marker, "0.0.05P2.1\n");
        return true;
    };
    assert(!package());
    assert(read(first) == "new map"); // No package-wide rollback.
    assert(read(second) == "old executable"); // Failed swap restored its original.
    assert(read(marker) == "0.0.05\n"); // Only a successful package commits marker.
    files.rejectedSource.clear();
    write(first + ".unbound-new", "new map");
    assert(package());
    assert(read(first) == "new map" && read(second) == "new executable");
    assert(read(marker) == "0.0.05P2.1\n");
    for (const auto& path : {first, second}) {
        assert(!std::filesystem::exists(path + ".unbound-new"));
        assert(!std::filesystem::exists(path + ".unbound-replacing"));
        assert(std::filesystem::remove(path));
    }
    assert(std::filesystem::remove(marker));
    assert(std::filesystem::remove(directory));
}
} // namespace

int main() {
    successfulCommits();
    failedCommits();
    interruptedStates();
    unsafePaths();
    markerLastAndRetry();
    realFileRoundtrip();
    realPackageReplay();
    std::puts("No-copy installer replacement, recovery, marker-last and retry tests passed.");
}
