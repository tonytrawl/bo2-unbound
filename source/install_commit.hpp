#pragma once

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace unbound {

enum class InstallPathState { Missing, RegularFile, Other, Error };

struct StdioInstallFileOps {
    InstallPathState inspect(const std::string& path) const {
        struct stat info {};
        if (stat(path.c_str(), &info) == 0)
            return S_ISREG(info.st_mode) ? InstallPathState::RegularFile
                                        : InstallPathState::Other;
        return errno == ENOENT ? InstallPathState::Missing : InstallPathState::Error;
    }

    bool renameFile(const std::string& source, const std::string& target) const {
        return std::rename(source.c_str(), target.c_str()) == 0;
    }

    bool removeFile(const std::string& path) const {
        return std::remove(path.c_str()) == 0;
    }
};

// Payloads must not own the installer marker or a replacement's working paths.
// Inspect every component so a directory cannot collide with a temporary file.
inline bool reservedInstallPayloadPath(std::string logicalPath) {
    std::transform(logicalPath.begin(), logicalPath.end(), logicalPath.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (logicalPath == "update/content/update.txt" ||
        logicalPath.rfind("update/content/update.txt/", 0) == 0)
        return true;
    std::size_t start = 0;
    for (;;) {
        const auto slash = logicalPath.find('/', start);
        const auto component = logicalPath.substr(start, slash == std::string::npos
                                                            ? std::string::npos : slash - start);
        if(component.find(".unbound-link-")!=std::string::npos)return true;
        for (const char* suffix : {".unbound-new", ".unbound-replacing", ".unbound-old"}) {
            const std::string ending(suffix);
            if (component.size() >= ending.size() &&
                component.compare(component.size() - ending.size(), ending.size(), ending) == 0)
                return true;
        }
        if (slash == std::string::npos) return false;
        start = slash + 1;
    }
}

// This is only a same-directory swap of one fully verified file, not a copied
// backup or package rollback. A previous file is kept just for the rename gap
// and removed immediately after that file commits. Repeating a failed package
// repairs files already committed by an earlier attempt.
template <class FileOps>
bool recoverInstallReplacement(const std::string& target, std::string& error,
                               FileOps& files) {
    const std::string previous = target + ".unbound-replacing";
    const auto currentState = files.inspect(target);
    const auto previousState = files.inspect(previous);
    if (currentState == InstallPathState::Error || previousState == InstallPathState::Error) {
        error = "Could not inspect an interrupted file replacement: " + target;
        return false;
    }
    if (currentState == InstallPathState::Other || previousState == InstallPathState::Other) {
        error = "A replacement path is not a regular file: " + target;
        return false;
    }
    if (previousState == InstallPathState::Missing) return true;
    if (currentState == InstallPathState::Missing) {
        if (!files.renameFile(previous, target)) {
            error = "Could not restore the interrupted replacement. Original file remains at " + previous;
            return false;
        }
    } else if (!files.removeFile(previous)) {
        error = "Could not finish cleanup of an interrupted replacement: " + previous;
        return false;
    }
    return true;
}

inline bool recoverInstallReplacement(const std::string& target, std::string& error) {
    StdioInstallFileOps files;
    return recoverInstallReplacement(target, error, files);
}

template <class FileOps>
bool commitInstallReplacement(const std::string& temporary, const std::string& target,
                              std::string& error, FileOps& files) {
    const std::string previous = target + ".unbound-replacing";
    if (temporary == target || temporary == previous) {
        error = "Invalid temporary replacement path";
        return false;
    }
    if (files.inspect(temporary) != InstallPathState::RegularFile) {
        error = "Verified replacement is not an accessible regular file: " + temporary;
        return false;
    }
    if (!recoverInstallReplacement(target, error, files)) return false;
    const auto currentState = files.inspect(target);
    if (currentState != InstallPathState::Missing && currentState != InstallPathState::RegularFile) {
        error = "Could not inspect the file being replaced: " + target;
        return false;
    }
    const bool hadOriginal = currentState == InstallPathState::RegularFile;
    if (hadOriginal && !files.renameFile(target, previous)) {
        error = "Could not prepare the existing file for replacement: " + target;
        return false;
    }
    if (!files.renameFile(temporary, target)) {
        if (hadOriginal && !files.renameFile(previous, target)) {
            error = "Could not commit or restore the replacement. Original file remains at " + previous;
        } else {
            error = "Could not commit the extracted file: " + target;
        }
        return false;
    }
    if (hadOriginal && !files.removeFile(previous)) {
        error = "The new file was written, but replacement cleanup failed: " + previous;
        return false;
    }
    return true;
}

inline bool commitInstallReplacement(const std::string& temporary, const std::string& target,
                                     std::string& error) {
    StdioInstallFileOps files;
    return commitInstallReplacement(temporary, target, error, files);
}

} // namespace unbound
