#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace unbound {

using DirectoryActivity = std::function<void(const char* operation, const std::string& path)>;

struct DirectoryStatResult {
    int error = 0;
    std::uint32_t mode = 0;
};

struct DirectoryProbeResult {
    int error = 0;
    bool closeFailed = false;
};

struct DirectoryFailure {
    std::string path;
    std::string operation;
    int error = 0;
    std::uint32_t mode = 0;
    bool modeKnown = false;

    std::string message() const {
        if (path.empty()) return "No destination directory was supplied";
        std::ostringstream text;
        text << "Destination folder check failed: " << path << "\n"
             << operation << "; errno=" << error << " (" << std::strerror(error) << ')';
        if (modeKnown) text << "; mode=0x" << std::hex << mode;
        if (modeKnown && S_ISLNK(mode))
            text << "\nExisting filesystem link/reference was left unchanged. HQ will not write through it.";
        else if (modeKnown && S_ISREG(mode))
            text << "\nAn existing regular file occupies the required folder path; it was left unchanged.";
        return text.str();
    }
};

// IO operations return captured errno values; a later probe must not overwrite
// the original failure. Only an unknown type may receive a read-only open test.
// In particular, successful opens never authorize following a typed link.
template<class Io, class Cache>
bool prepareDirectoryPrefixes(const std::string& path,
                              const std::vector<std::string>& prefixes,
                              Cache& verified, Io& io, DirectoryFailure& failure,
                              const DirectoryActivity& activity = {}) {
    failure = {};
    const auto fail = [&](const std::string& current, const char* operation,
                          int error, std::uint32_t mode = 0, bool modeKnown = false) {
        failure = {current, operation, error ? error : EIO, mode, modeKnown};
        return false;
    };
    if (path.empty()) return fail(path, "validate path", EINVAL);
    if (verified.find(path) != verified.end()) return true;
    const auto inspect = [&](const std::string& current) {
        if (activity) activity("stat", current);
        return io.inspect(current);
    };
    const auto checkExisting = [&](const std::string& current,
                                   const DirectoryStatResult& state, const char* operation) {
        if (state.error) return fail(current, operation, state.error);
        if (S_ISDIR(state.mode)) return true;
        if ((state.mode & S_IFMT) != 0)
            return fail(current, "stat: existing entry is not a directory", ENOTDIR, state.mode, true);
        if (activity) activity("opendir/closedir", current);
        const auto probe = io.probe(current);
        if (probe.error)
            return fail(current, probe.closeFailed ? "closedir: unknown-type entry" :
                                                    "opendir: unknown-type entry",
                        probe.error, state.mode, true);
        return true;
    };
    // Device/mount roots cannot be created. An empty prefix list is a read-only
    // existence/type check, matching the former ensureDirectory root behavior.
    if (prefixes.empty()) {
        if (!checkExisting(path, inspect(path), "stat: mount root")) return false;
        verified.insert(path);
        return true;
    }
    for (const auto& current : prefixes) {
        if (verified.find(current) != verified.end()) continue;
        const auto state = inspect(current);
        if (!state.error) {
            if (!checkExisting(current, state, "stat")) return false;
        } else {
            if (state.error != ENOENT) return fail(current, "stat: refusing mkdir after non-ENOENT failure", state.error);
            if (activity) activity("mkdir", current);
            const auto made = io.make(current);
            if (made && made != EEXIST) return fail(current, "mkdir", made);
            if (made == EEXIST &&
                !checkExisting(current, inspect(current), "stat after mkdir EEXIST")) return false;
        }
        verified.insert(current);
    }
    return true;
}

} // namespace unbound
