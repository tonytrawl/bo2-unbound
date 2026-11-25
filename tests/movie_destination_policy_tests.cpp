// Host integration of production routing, prefix derivation, directory policy
// and file commit. The only adapter is a device alias mapped to a task-owned
// temporary filesystem; Wii U reference semantics still need hardware testing.
#include "directory_policy.hpp"
#include "install_commit.hpp"
#include "model.hpp"
#include "movie_install_path.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {
using namespace unbound;
namespace fs = std::filesystem;
const std::string Alias = "storage_usb01:/";
const std::string Update = Alias + "usr/title/0005000e/1010cf00";

void write(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    assert(output.good());
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
struct OwnedTemp {
    fs::path root;
    OwnedTemp() {
        char pattern[] = "/tmp/unbound-movie-destination-XXXXXX";
        const auto* made = mkdtemp(pattern);
        assert(made);
        root = fs::path(made);
        assert(root.parent_path() == "/tmp");
        assert(root.filename().string().starts_with("unbound-movie-destination-"));
    }
    ~OwnedTemp() {
        // Delete only this exact mkdtemp-owned real directory. remove_all does
        // not follow child directory symlinks, including the mock stock link.
        assert(root.parent_path() == "/tmp" &&
               root.filename().string().starts_with("unbound-movie-destination-"));
        assert(!fs::is_symlink(fs::symlink_status(root)));
        assert(fs::canonical(root) == root);
        std::error_code error;
        fs::remove_all(root, error);
        assert(!error);
    }
};
struct NativeDirectoryIo {
    fs::path volume;
    std::vector<std::string> inspections, creations, probes;
    fs::path disk(const std::string& path) const {
        assert(path.rfind(Alias, 0) == 0);
        const auto suffix = path.substr(Alias.size());
        assert(suffix.find("..") == std::string::npos && suffix.find('\\') == std::string::npos);
        return volume / suffix;
    }
    DirectoryStatResult inspect(const std::string& path) {
        inspections.push_back(path);
        struct stat state {};
        errno = 0;
        if (lstat(disk(path).c_str(), &state) == 0)
            return {0, static_cast<std::uint32_t>(state.st_mode)};
        return {errno ? errno : EIO, 0};
    }
    int make(const std::string& path) {
        creations.push_back(path);
        errno = 0;
        return mkdir(disk(path).c_str(), 0777) == 0 ? 0 : (errno ? errno : EIO);
    }
    DirectoryProbeResult probe(const std::string& path) {
        probes.push_back(path);
        errno = 0;
        DIR* directory = opendir(disk(path).c_str());
        if (!directory) return {errno ? errno : EIO, false};
        errno = 0;
        return closedir(directory) == 0 ? DirectoryProbeResult{} :
               DirectoryProbeResult{errno ? errno : EIO, true};
    }
};
struct Fixture {
    fs::path root, content, baseVideo;
    NativeDirectoryIo io;
    struct stat originalReference {};
    explicit Fixture(const fs::path& parent, const std::string& name)
        : root(parent / name), io{root / "volume", {}, {}, {}} {
        content = io.disk(Update + "/content");
        baseVideo = root / "base-video";
        fs::create_directories(content / "english");
        fs::create_directories(baseVideo);
        write(baseVideo / "stock.bik", "retail movie bytes must remain unchanged");
        fs::create_directory_symlink(baseVideo, content / "video");
        assert(lstat((content / "video").c_str(), &originalReference) == 0 &&
               S_ISLNK(originalReference.st_mode));
    }
    void assertStockUnchanged() const {
        struct stat after {};
        assert(lstat((content / "video").c_str(), &after) == 0 && S_ISLNK(after.st_mode));
        assert(after.st_ino == originalReference.st_ino && after.st_dev == originalReference.st_dev);
        assert(fs::read_symlink(content / "video") == baseVideo);
        assert(read(baseVideo / "stock.bik") == "retail movie bytes must remain unchanged");
        for (const auto& path : io.inspections) assert(path != Update + "/content/video");
        for (const auto& path : io.creations) assert(path != Update + "/content/video");
    }
};

std::string physical(const std::string& destination) {
    assert(destination.rfind("update/", 0) == 0);
    return Update + "/" + destination.substr(7);
}
bool prepare(const MovieInstallPath& route, Fixture& fixture,
             std::unordered_set<std::string>& verified, DirectoryFailure& failure) {
    assert(route.status == MovieInstallPathStatus::Movie);
    const auto target = physical(route.movieDirectory);
    return prepareDirectoryPrefixes(target, directoryPrefixes(target), verified,
                                    fixture.io, failure);
}

void freshDestinationAndRealReplacement(const fs::path& root) {
    Fixture fixture(root, "fresh");
    const auto route = routeMovieInstallPath("update/content/video/zm_highrise_load.bik", "english");
    assert(route.destination == "update/content/english/video/zm_highrise_load.bik");
    assert(!fs::exists(fixture.content / "english/video"));
    std::unordered_set<std::string> verified;
    DirectoryFailure failure;
    assert(prepare(route, fixture, verified, failure));
    assert(fixture.io.creations == std::vector<std::string>{Update + "/content/english/video"});
    assert(fs::is_directory(fixture.content / "english/video") && fixture.io.probes.empty());
    fixture.assertStockUnchanged();

    // The already-verified prefix cache is the same one used by extraction.
    const auto inspections = fixture.io.inspections.size();
    assert(prepare(route, fixture, verified, failure));
    assert(fixture.io.inspections.size() == inspections && fixture.io.creations.size() == 1);

    const auto movie = fixture.io.disk(physical(route.destination));
    const auto temporary = fs::path(movie.string() + ".unbound-new");
    const std::string first("BIK\0first fixture", 17);
    write(temporary, first);
    std::string error;
    // Production commit expects a fully verified, closed temporary file. ZIP
    // decoding/CRC is independently tested by the installer integration suite.
    assert(commitInstallReplacement(temporary.string(), movie.string(), error));
    assert(read(movie) == first && !fs::exists(temporary));
    assert(!fs::exists(movie.string() + ".unbound-replacing"));
    write(temporary, "new verified movie fixture");
    assert(commitInstallReplacement(temporary.string(), movie.string(), error));
    assert(read(movie) == "new verified movie fixture" && !fs::exists(temporary));
    assert(!fs::exists(movie.string() + ".unbound-replacing"));
    fixture.assertStockUnchanged();
}

void referencedMovieDirectoryIsRefused(const fs::path& root) {
    Fixture fixture(root, "localized-reference");
    const auto linked = fixture.root / "linked-localized-video";
    fs::create_directories(linked);
    write(linked / "sentinel.bik", "untouched localized reference target");
    fs::create_directory_symlink(linked, fixture.content / "english/video");
    const auto route = routeMovieInstallPath("update/content/video/zm_highrise_load.bik", "english");
    std::unordered_set<std::string> verified;
    DirectoryFailure failure;
    assert(!prepare(route, fixture, verified, failure));
    assert(failure.error == ENOTDIR && S_ISLNK(failure.mode));
    assert(failure.path == Update + "/content/english/video");
    assert(fixture.io.creations.empty() && fixture.io.probes.empty());
    assert(fs::is_symlink(fs::symlink_status(fixture.content / "english/video")));
    assert(read(linked / "sentinel.bik") == "untouched localized reference target");
    assert(!fs::exists(linked / "zm_highrise_load.bik"));
    fixture.assertStockUnchanged();
}

void referencedLanguageAncestorIsRefused(const fs::path& root) {
    Fixture fixture(root, "language-reference");
    assert(fs::remove(fixture.content / "english")); // Known empty fixture directory.
    const auto linked = fixture.root / "linked-language";
    fs::create_directories(linked);
    write(linked / "sentinel.txt", "untouched language reference target");
    fs::create_directory_symlink(linked, fixture.content / "english");
    const auto route = routeMovieInstallPath("update/content/english/video/a.bik", "english");
    std::unordered_set<std::string> verified;
    DirectoryFailure failure;
    assert(!prepare(route, fixture, verified, failure));
    assert(failure.error == ENOTDIR && S_ISLNK(failure.mode));
    assert(failure.path == Update + "/content/english");
    assert(fixture.io.creations.empty() && fixture.io.probes.empty());
    assert(!fs::exists(linked / "video"));
    assert(read(linked / "sentinel.txt") == "untouched language reference target");
    fixture.assertStockUnchanged();
}

void occupiedMovieDirectoryIsRefused(const fs::path& root) {
    Fixture fixture(root, "occupied-destination");
    write(fixture.content / "english/video", "existing user file");
    const auto route = routeMovieInstallPath("update/content/video/a.bik", "english");
    std::unordered_set<std::string> verified;
    DirectoryFailure failure;
    assert(!prepare(route, fixture, verified, failure));
    assert(failure.error == ENOTDIR && S_ISREG(failure.mode));
    assert(fixture.io.creations.empty());
    assert(read(fixture.content / "english/video") == "existing user file");
    fixture.assertStockUnchanged();
}

void mappingCollisionsAreVisibleBeforeWriting() {
    for (const auto& [language, explicitLanguage] : {
            std::pair{"english", "english"}, {"japanese", "fulljap"}, {"fulljap", "japanese"}}) {
        const auto global = routeMovieInstallPath("update/content/video/zm_highrise_load.bik", language);
        const auto localized = routeMovieInstallPath(std::string("update/content/") +
                                                    explicitLanguage + "/video/zm_highrise_load.bik", language);
        assert(global.status == MovieInstallPathStatus::Movie && localized.status == MovieInstallPathStatus::Movie);
        std::unordered_set<std::string> mappedFiles;
        assert(mappedFiles.insert(physical(global.destination)).second);
        assert(!mappedFiles.insert(physical(localized.destination)).second);
    }
}
} // namespace

int main() {
    OwnedTemp temporary;
    freshDestinationAndRealReplacement(temporary.root);
    referencedMovieDirectoryIsRefused(temporary.root);
    referencedLanguageAncestorIsRefused(temporary.root);
    occupiedMovieDirectoryIsRefused(temporary.root);
    mappingCollisionsAreVisibleBeforeWriting();
    std::cout << "Movie destination policy: real localized mkdir/commit, untouched shared reference and stock bytes, linked ancestors refused, cached prefixes and mapped collisions passed\n";
}
