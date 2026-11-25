#include "map_packs.hpp"
#include "platform.hpp"
#include "config.hpp"
#include "io_buffer.hpp"
#include "buffered_zip.hpp"
#include "download_pipe.hpp"
#include "download_resume.hpp"
#include "download_control.hpp"
#include "download_size.hpp"
#include "background_operation.hpp"
#include "operation_metrics.hpp"
#include "install_stream.hpp"
#include "install_commit.hpp"
#include "install_activity.hpp"
#include "install_discovery.hpp"
#include "archive_limits.hpp"
#include "tester_archive.hpp"
#include "directory_policy.hpp"
#include "title_folder_link.hpp"
#include "inherited_content.hpp"
#include "retained_reference_restore.hpp"
#include "native_link.hpp"
#include "movie_install_path.hpp"
#include "disc_source.hpp"
#include "app_path_migration.hpp"
#include "game_launch.hpp"
#include <nn/acp.h>
#include "core_recovery.hpp"
#include "package_cache_reset.hpp"
#include "package_verification_policy.hpp"
#include "local_package_state.hpp"
#include "support_archive.hpp"
#include "sha256.hpp"
#include "https_curl.hpp"

#include <coreinit/mcp.h>
#include <coreinit/filesystem.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <curl/curl.h>
#include <mocha/mocha.h>
#include <nn/ac/ac_c.h>
#include <rpxloader/rpxloader.h>
#include <SDL2/SDL.h>
#include <zip.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace unbound {
namespace {
static_assert(sizeof(off_t) >= 8, "HQ requires 64-bit file offsets for large ZIPs");
static_assert(sizeof(decltype(std::declval<struct stat>().st_size)) >= 8, "HQ requires 64-bit file sizes");
bool mochaReady = false;
bool mlcMounted = false;
bool usbMounted = false;
bool discSourceMounted[DiscSourceVolumeCount]{};
bool curlReady = false;
std::function<void()> operationFrameCallback;

using ByteObserver = std::function<void(std::size_t)>;
using ModSyncLedger = std::map<std::string, std::string>;

std::string parentPath(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

bool exists(const std::string& path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0;
}

bool recoverAtomicTextFile(const std::string& path) {
    const std::string temporary = path + ".unbound-new";
    const std::string previous = path + ".unbound-old";
    if (!exists(path) && exists(previous) &&
        std::rename(previous.c_str(), path.c_str()) != 0)
        return false;
    if (exists(path) && exists(previous) && std::remove(previous.c_str()) != 0)
        return false;
    if (exists(temporary) && std::remove(temporary.c_str()) != 0)
        return false;
    return true;
}

bool freeSpaceForPath(const std::string& path, unsigned long long& bytes) {
    const char* volume = nullptr;
    if (path.rfind("fs:/vol/external01", 0) == 0) volume = "/vol/external01";
    else if (path.rfind("storage_mlc01:", 0) == 0) volume = "/vol/storage_mlc01";
    else if (path.rfind("storage_usb01:", 0) == 0) volume = "/vol/storage_usb01";
    if (!volume) return false;
    alignas(64) FSClient client{};
    alignas(64) FSCmdBlock block{};
    if (FSAddClient(&client, FS_ERROR_FLAG_NONE) < 0) return false;
    FSInitCmdBlock(&block);
    std::uint64_t available = 0;
    const bool ok = FSGetFreeSpaceSize(&client, &block, volume, &available, FS_ERROR_FLAG_NONE) >= 0;
    FSDelClient(&client, FS_ERROR_FLAG_NONE);
    bytes = available;
    return ok;
}

bool hasSpaceFor(const std::string& path, unsigned long long required, std::string& error) {
    constexpr unsigned long long Reserve = 8ULL * 1024 * 1024;
    unsigned long long available = 0;
    if (!freeSpaceForPath(path, available)) {
        error = "Could not determine free space for " + path;
        return false;
    }
    if (required > available || Reserve > available - required) {
        error = "Not enough free space. Required: " + std::to_string((required + Reserve + 1023) / 1024) +
                " KB; available: " + std::to_string(available / 1024) + " KB";
        return false;
    }
    return true;
}

bool hasReservedModSuffix(const std::string& folder) {
    std::string folded = folder;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return
        (folded.size() >= 12 &&
         (folded.rfind(".unbound-new") == folded.size() - 12 ||
          folded.rfind(".unbound-old") == folded.size() - 12)) ||
        (folded.size() >= 15 &&
         folded.rfind(".unbound-delete") == folded.size() - 15);
}

bool validModFolder(const std::string& folder) {
    const bool hasControl = std::any_of(folder.begin(), folder.end(), [](unsigned char value) {
        return value < 0x20 || value == 0x7f;
    });
    std::string folded = folder;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return !folder.empty() && folder.size() <= 128 && !hasControl &&
           folder.find('/') == std::string::npos &&
           safeRelativePath(folder) && folded.rfind(".unbound-", 0) != 0 &&
           !hasReservedModSuffix(folder);
}

const char* deviceKey(Device device) {
    switch (device) {
    case Device::MLC: return "mlc";
    case Device::USB: return "usb";
    default: return "unknown";
    }
}

std::string caseFoldedModFolder(std::string folder) {
    std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return folder;
}

// Wii U storage is normally case-sensitive, but treating mod folder names as
// case-insensitive avoids creating two visually identical mods and catches
// collisions with folders that do not contain a modload.txt manifest.
bool findCaseInsensitiveChild(const std::string& root, const std::string& wanted,
                              bool& found, std::string& actual,
                              std::string& error) {
    found = false;
    actual.clear();
    if (!exists(root)) return true;
    DIR* directory = opendir(root.c_str());
    if (!directory) {
        error = "Could not inspect " + root + " for folder-name collisions";
        return false;
    }
    const std::string foldedWanted = caseFoldedModFolder(wanted);
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == ".." ||
            caseFoldedModFolder(name) != foldedWanted)
            continue;
        if (found) {
            closedir(directory);
            error = "More than one entry in " + root +
                    " differs from " + wanted + " only by letter case";
            return false;
        }
        found = true;
        actual = name;
    }
    closedir(directory);
    return true;
}

ModSyncLedger::iterator findLedgerFolder(ModSyncLedger& ledger,
                                         const std::string& folder) {
    const std::string folded = caseFoldedModFolder(folder);
    return std::find_if(ledger.begin(), ledger.end(), [&](const auto& item) {
        return caseFoldedModFolder(item.first) == folded;
    });
}

std::string modSyncLedgerPath(const GameInstall& install) {
    if (!install.sku || !install.update.present) return {};
    char titleId[17]{};
    std::snprintf(titleId, sizeof(titleId), "%016llx",
                  static_cast<unsigned long long>(install.sku->updateTitleId));
    return std::string(config::gameStateRoot()) + "/mods-" + titleId + "-" +
           deviceKey(install.update.device) + ".txt";
}

char hexDigit(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string hexEncode(const std::string& value) {
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        encoded.push_back(hexDigit(byte >> 4));
        encoded.push_back(hexDigit(byte & 0x0f));
    }
    return encoded;
}

bool hexDecode(const std::string& encoded, std::string& value) {
    if (encoded.size() % 2 != 0 || encoded.size() > 512) return false;
    value.clear();
    value.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const int high = hexValue(encoded[index]);
        const int low = hexValue(encoded[index + 1]);
        if (high < 0 || low < 0) return false;
        value.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool validModSyncToken(const std::string& token) {
    if (token.empty()) return true;
    const bool knownKey = token.rfind("version=", 0) == 0 ||
                          token.rfind("sync_id=", 0) == 0;
    return knownKey && token.size() <= 136 &&
        std::none_of(token.begin(), token.end(), [](unsigned char value) {
            return value < 0x20 || value == 0x7f;
        });
}

bool loadModSyncLedger(const GameInstall& install, ModSyncLedger& ledger,
                       std::string& error) {
    ledger.clear();
    const std::string path = modSyncLedgerPath(install);
    if (path.empty()) {
        error = "The game update is not available";
        return false;
    }
    if (!recoverAtomicTextFile(path)) {
        error = "Could not recover the SD Auto-Sync ownership ledger";
        return false;
    }
    if (!exists(path)) return true;
    std::string text;
    if (!readTextFile(path, text, config::MaxModSyncLedgerBytes)) {
        error = "Could not read the SD Auto-Sync ownership ledger";
        return false;
    }
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    std::unordered_set<std::string> foldedFolders;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('|');
        if (separator == std::string::npos ||
            line.find('|', separator + 1) != std::string::npos) {
            error = "The SD Auto-Sync ownership ledger is malformed at line " +
                    std::to_string(lineNumber);
            ledger.clear();
            return false;
        }
        std::string folder;
        std::string token;
        if (!hexDecode(line.substr(0, separator), folder) ||
            !validModFolder(folder) ||
            !hexDecode(line.substr(separator + 1), token) ||
            !validModSyncToken(token) ||
            !foldedFolders.insert(caseFoldedModFolder(folder)).second ||
            !ledger.emplace(folder, token).second) {
            error = "The SD Auto-Sync ownership ledger is unsafe at line " +
                    std::to_string(lineNumber);
            ledger.clear();
            return false;
        }
    }
    return true;
}

bool saveModSyncLedger(const GameInstall& install, const ModSyncLedger& ledger,
                       std::string& error) {
    const std::string path = modSyncLedgerPath(install);
    if (path.empty()) {
        error = "The game update is not available";
        return false;
    }
    std::string text =
        "# Black Ops 2 Unbound HQ SD Auto-Sync ownership ledger\n"
        "# hex-encoded folder|hex-encoded manifest version/sync_id token\n";
    for (const auto& [folder, token] : ledger)
        text += hexEncode(folder) + "|" + hexEncode(token) + "\n";
    if (text.size() > config::MaxModSyncLedgerBytes) {
        error = "The SD Auto-Sync ownership ledger exceeds its safety limit";
        return false;
    }
    if (!writeTextFileAtomic(path, text, error)) {
        error = "Could not save the SD Auto-Sync ownership ledger: " + error;
        return false;
    }
    return true;
}

bool installedFolderMatchesOwnership(const std::string& target,
                                     const std::string& ownedToken) {
    struct stat targetInfo{};
    if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode))
        return false;
    // Legacy tokenless manifests intentionally re-copy every run. Their
    // ownership can only be represented by the per-install ledger entry.
    if (ownedToken.empty()) return true;
    std::string installedManifest;
    return readTextFile(target + "/modload.txt", installedManifest, 16 * 1024) &&
           modSyncTokenFromManifest(installedManifest) == ownedToken;
}

bool removeTree(const std::string& path) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) return errno == ENOENT;
    if (S_ISREG(info.st_mode)) return std::remove(path.c_str()) == 0;
    if (!S_ISDIR(info.st_mode)) return false;
    DIR* directory = opendir(path.c_str());
    if (!directory) return false;
    bool ok = true;
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (!removeTree(path + "/" + name)) { ok = false; break; }
    }
    closedir(directory);
    return ok && rmdir(path.c_str()) == 0;
}

bool countTreeFiles(const std::string& path, unsigned depth, unsigned& files,
                    unsigned long long& bytes) {
    if (depth > 32) return false;
    DIR* directory = opendir(path.c_str());
    if (!directory) return false;
    bool ok = true;
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        const std::string child = path + "/" + name;
        struct stat info{};
        if (stat(child.c_str(), &info) != 0) { ok = false; break; }
        if (S_ISDIR(info.st_mode)) {
            if (!countTreeFiles(child, depth + 1, files, bytes)) { ok = false; break; }
        } else if (S_ISREG(info.st_mode)) {
            ++files; bytes += static_cast<unsigned long long>(info.st_size);
            if (files > config::MaxArchiveFiles || bytes > config::MaxArchiveBytes) { ok = false; break; }
        } else { ok = false; break; }
    }
    closedir(directory);
    return ok;
}

struct PosixDirectoryIo {
    DirectoryStatResult inspect(const std::string& path) {
        struct stat info{};
        errno = 0;
        if (stat(path.c_str(), &info) == 0)
            return {0, static_cast<std::uint32_t>(info.st_mode)};
        return {errno ? errno : EIO, 0};
    }

    int make(const std::string& path) {
        errno = 0;
        return mkdir(path.c_str(), 0777) == 0 ? 0 : (errno ? errno : EIO);
    }

    DirectoryProbeResult probe(const std::string& path) {
        errno = 0;
        DIR* directory = opendir(path.c_str());
        if (!directory) return {errno ? errno : EIO, false};
        errno = 0;
        return closedir(directory) == 0 ? DirectoryProbeResult{} :
                                        DirectoryProbeResult{errno ? errno : EIO, true};
    }
};

struct FileIoContext {
    AlignedIoBuffer transfer;
    AlignedIoBuffer input{64 * 1024};
    AlignedIoBuffer output;
    std::unordered_set<std::string> directories;
    DirectoryFailure directoryFailure;

    bool ready() const { return transfer && input && output; }

    bool prepareDirectory(const std::string& path, const DirectoryActivity& activity = {}) {
        PosixDirectoryIo io;
        const bool ok = prepareDirectoryPrefixes(path, directoryPrefixes(path), directories, io,
                                                 directoryFailure, activity);
        if (!ok) errno = directoryFailure.error;
        return ok;
    }

    std::string directoryError() const {
        const int savedError = directoryFailure.error;
        // Reporting a failed stat/open must not issue another filesystem
        // operation. In particular, never open a typed link just to diagnose
        // why it was rejected: native FS calls can wait on that same entry.
        const auto detail = directoryFailure.message();
        errno = savedError;
        return detail;
    }
};

bool copyFileBuffered(const std::string& from, const std::string& to,
                      FileIoContext& io, const ByteObserver& observer = {}) {
    if (!io.ready() || !io.prepareDirectory(parentPath(to))) return false;
    FILE* input = std::fopen(from.c_str(), "rb");
    if (!input) return false;
    if (std::setvbuf(input, io.input.data(), _IOFBF, io.input.size()) != 0) {
        std::fclose(input);
        return false;
    }
    FILE* output = std::fopen(to.c_str(), "wb");
    if (!output) { std::fclose(input); return false; }
    bool ok = std::setvbuf(output, io.output.data(), _IOFBF, io.output.size()) == 0;
    while (ok) {
        const auto count = std::fread(io.transfer.data(), 1, io.transfer.size(), input);
        if (!count) break;
        if (std::fwrite(io.transfer.data(), 1, count, output) != count) { ok = false; break; }
        if (observer) observer(count);
    }
    if (std::ferror(input)) ok = false;
    if (std::fclose(output) != 0) ok = false;
    if (std::fclose(input) != 0) ok = false;
    if (!ok) std::remove(to.c_str());
    return ok;
}

bool copyTree(const std::string& source, const std::string& target, unsigned depth,
              unsigned long long totalBytes, unsigned long long& copiedBytes,
              int& lastProgress,
              std::uint32_t& lastProgressTick,
              const ProgressCallback& progress, FileIoContext& io) {
    if (depth > 32 || !io.prepareDirectory(target)) return false;
    DIR* directory = opendir(source.c_str());
    if (!directory) return false;
    bool ok = true;
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        const std::string from = source + "/" + name;
        const std::string to = target + "/" + name;
        struct stat info{};
        if (stat(from.c_str(), &info) != 0) { ok = false; break; }
        if (S_ISDIR(info.st_mode)) {
            if (!copyTree(from, to, depth + 1, totalBytes, copiedBytes,
                          lastProgress, lastProgressTick, progress, io)) { ok = false; break; }
        } else if (S_ISREG(info.st_mode)) {
            const auto observe = [&](std::size_t count) {
                copiedBytes += count;
                const int percent = totalBytes
                    ? std::clamp(static_cast<int>((copiedBytes * 100ULL) / totalBytes), 0, 100)
                    : 0;
                const std::uint32_t now = SDL_GetTicks();
                const std::uint32_t elapsed = now - lastProgressTick;
                if (progress && (lastProgress < 0 ||
                    (percent != lastProgress && elapsed >= 100) || elapsed >= 1000)) {
                    lastProgress = percent;
                    lastProgressTick = now;
                    progress("Copying SD mod", "Copying " + name + "...", percent);
                }
            };
            if (!copyFileBuffered(from, to, io, observe)) { ok = false; break; }
        } else { ok = false; break; }
    }
    closedir(directory);
    return ok;
}

using ModOwnershipCommit = std::function<bool(std::string&)>;

bool recoverInterruptedModCopy(const std::string& target, std::string& error) {
    const std::string root = parentPath(target);
    const std::string leaf = target.substr(target.find_last_of('/') + 1);
    bool temporaryFound = false;
    bool backupFound = false;
    std::string temporaryLeaf;
    std::string backupLeaf;
    if (!findCaseInsensitiveChild(root, leaf + ".unbound-new",
                                  temporaryFound, temporaryLeaf, error) ||
        !findCaseInsensitiveChild(root, leaf + ".unbound-old",
                                  backupFound, backupLeaf, error))
        return false;
    const std::string temporary = root + "/" +
        (temporaryFound ? temporaryLeaf : leaf + ".unbound-new");
    const std::string backup = root + "/" +
        (backupFound ? backupLeaf : leaf + ".unbound-old");
    if (temporaryFound) {
        error = "A reserved temporary folder already exists at " + temporary +
                ". HQ left it untouched; inspect or remove it before retrying.";
        return false;
    }
    if (backupFound) {
        error = "A reserved backup folder already exists at " + backup +
                ". HQ cannot prove who created it, so it was left untouched. "
                "Inspect and recover or remove it manually before retrying.";
        return false;
    }
    return true;
}

bool reconcileInterruptedModDeletes(const std::string& modsRoot, std::string& error) {
    if (!exists(modsRoot)) return true;
    DIR* directory = opendir(modsRoot.c_str());
    if (!directory) {
        error = "Could not inspect interrupted mod deletions";
        return false;
    }
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (!hasReservedModSuffix(name)) continue;
        const std::string pending = modsRoot + "/" + name;
        closedir(directory);
        error = "A reserved mod transaction folder already exists at " + pending +
                ". HQ cannot prove who created it, so it was left untouched. "
                "Inspect and recover or remove it manually before retrying.";
        return false;
    }
    closedir(directory);
    return true;
}

bool copyModFolderTransactional(const std::string& source, const std::string& target,
                                unsigned long long bytes,
                                const ProgressCallback& progress,
                                const ModOwnershipCommit& commitOwnership,
                                std::string& error) {
    const std::string modsRoot = parentPath(target);
    const std::string temporary = target + ".unbound-new";
    const std::string backup = target + ".unbound-old";
    if (!ensureDirectory(modsRoot)) {
        error = "Could not create the destination mods folder";
        return false;
    }
    // Recover a power loss between preserving the previous folder and
    // committing the new folder. Never erase the only known-good copy.
    if (!recoverInterruptedModCopy(target, error)) return false;
    if (!hasSpaceFor(target, bytes, error)) return false;
    unsigned long long copiedBytes = 0;
    int lastProgress = -1;
    std::uint32_t lastProgressTick = 0;
    FileIoContext io;
    if (!io.ready()) { error = "Could not allocate mod copy buffers"; return false; }
    if (!copyTree(source, temporary, 0, bytes, copiedBytes, lastProgress,
                  lastProgressTick, progress, io)) {
        removeTree(temporary);
        error = "Could not copy every mod file";
        return false;
    }
    const bool replacing = exists(target);
    if (replacing && std::rename(target.c_str(), backup.c_str()) != 0) {
        removeTree(temporary);
        error = "Could not preserve the installed mod before replacing it";
        return false;
    }
    if (std::rename(temporary.c_str(), target.c_str()) != 0) {
        if (replacing) std::rename(backup.c_str(), target.c_str());
        removeTree(temporary);
        error = "Could not finish committing the copied mod";
        return false;
    }
    std::string commitError;
    if (commitOwnership && !commitOwnership(commitError)) {
        const bool removedNew = removeTree(target);
        const bool restored = replacing
            ? (removedNew && std::rename(backup.c_str(), target.c_str()) == 0)
            : removedNew;
        error = commitError;
        if (!restored) {
            error += "\nThe ownership record failed and the previous mod folder could not be "
                     "restored automatically. Check update/content/mods/" +
                     target.substr(target.find_last_of('/') + 1) + " before retrying.";
        }
        return false;
    }
    if (replacing) removeTree(backup);
    return true;
}

Device deviceFromTitle(const MCPTitleListType& title) {
    std::string path(title.path, strnlen(title.path, sizeof(title.path)));
    std::string device(title.indexedDevice, strnlen(title.indexedDevice, sizeof(title.indexedDevice)));
    if (path.find("storage_usb") != std::string::npos || device.find("usb") != std::string::npos)
        return Device::USB;
    if (path.find("storage_mlc") != std::string::npos || device.find("mlc") != std::string::npos)
        return Device::MLC;
    if (path.find("storage_odd") != std::string::npos || device.find("odd") != std::string::npos)
        return Device::Disc;
    return Device::Missing;
}

bool matchingDiscTitle(std::uint64_t titleId,MCPTitleListType& disc) {
    const int handle=MCP_Open();
    if(handle<0)return false;
    bool found=MCP_GetTitleInfo(handle,titleId,&disc)>=0&&disc.titleId==titleId&&deviceFromTitle(disc)==Device::Disc;
    if(!found){
        alignas(64) MCPTitleListType titles[16]{};std::uint32_t count=16;
        if(MCP_TitleListByDevice(handle,"odd",&count,titles,sizeof(titles))>=0)
            for(std::uint32_t i=0;i<std::min<std::uint32_t>(count,16);++i)
                if(titles[i].titleId==titleId&&deviceFromTitle(titles[i])==Device::Disc){disc=titles[i];found=true;break;}
    }
    MCP_Close(handle);return found;
}

std::string titleRoot(Device device, std::uint64_t titleId) {
    char path[128];
    std::snprintf(path, sizeof(path), "%s:/usr/title/%08x/%08x",
        device == Device::USB ? "storage_usb01" : "storage_mlc01",
        static_cast<unsigned>(titleId >> 32), static_cast<unsigned>(titleId));
    return path;
}

bool mountDevice(Device device) {
    if (device == Device::MLC) {
        if (mlcMounted) return true;
        auto result = Mocha_MountFS("storage_mlc01", nullptr, "/vol/storage_mlc01");
        mlcMounted = result == MOCHA_RESULT_SUCCESS;
        return mlcMounted;
    }
    if (device == Device::USB) {
        if (usbMounted) return true;
        auto result = Mocha_MountFS("storage_usb01", nullptr, "/vol/storage_usb01");
        usbMounted = result == MOCHA_RESULT_SUCCESS;
        return usbMounted;
    }
    return false;
}

void appendInstallWarning(std::string& warnings, const std::string& warning) {
    if (warning.empty()) return;
    if (!warnings.empty()) warnings += "\n";
    warnings += warning;
}

struct InstallDiscoveryIo {
    InstallDiscoveryPath pathState(const std::string& path) {
        struct stat information{};
        if (stat(path.c_str(), &information) != 0)
            return errno == ENOENT ? InstallDiscoveryPath::Missing : InstallDiscoveryPath::Unreadable;
        if (S_ISDIR(information.st_mode)) return InstallDiscoveryPath::Directory;
        if (S_ISREG(information.st_mode)) return InstallDiscoveryPath::File;
        return InstallDiscoveryPath::Other;
    }
    bool readText(const std::string& path, std::string& output, std::size_t maximum) {
        return readTextFile(path, output, maximum);
    }
    bool listDirectory(const std::string& path, std::vector<std::string>& names,
                       std::size_t maximum, std::string& error) {
        names.clear();
        DIR* directory = opendir(path.c_str());
        if (!directory) { error = "Could not inspect language folders in " + path; return false; }
        bool ok = true;
        while (true) {
            errno = 0;
            dirent* entry = readdir(directory);
            if (!entry) {
                if (errno != 0) { error = "Could not finish reading " + path; ok = false; }
                break;
            }
            if (names.size() >= maximum) {
                error = "The content folder has too many entries to scan safely: " + path;
                ok = false;
                break;
            }
            names.emplace_back(entry->d_name);
        }
        if (closedir(directory) != 0 && ok) {
            error = "Could not finish inspecting " + path;
            ok = false;
        }
        return ok;
    }
};

TitleProbe probeStockTitle(Device device, std::uint64_t titleId,
                          InstallLocation& location, std::string& error) {
    InstallDiscoveryIo io;
    return discoverStockTitle(device, titleId, location, error, io);
}

bool scanInstallLanguages(const InstallLocation& location,
                          std::vector<std::string>& languages, std::string& error) {
    InstallDiscoveryIo io;
    return discoverInstallLanguages(location, languages, error, io);
}

std::string currentInstallLocale() {
    std::string locale = "en";
    if (SDL_Locale* locales = SDL_GetPreferredLocales()) {
        if (locales[0].language) locale = locales[0].language;
        SDL_free(locales);
    }
    return locale;
}

void findPhysicalBase(GameInstall& install, const GameInstall* previous = nullptr) {
    install.base = {};
    if (!install.sku) return;
    const Device first = install.update.device;
    const Device second = first == Device::USB ? Device::MLC : Device::USB;
    std::string ignored;
    for (const Device device : {first, second}) {
        const bool mounted = device == Device::USB ? usbMounted : mlcMounted;
        if (mounted && probeStockTitle(device, install.sku->baseTitleId,
                                      install.base, ignored) == TitleProbe::Valid)
            return;
    }
    if (previous && previous->sku == install.sku && previous->base.present &&
        previous->base.device == Device::Disc) {
        install.base = previous->base;
        return;
    }
    // The per-ID lookup is safe and bounded. Only take an actual matching
    // disc result here; MLC/USB copies require the physical probe above.
    const int handle = MCP_Open();
    if (handle < 0) return;
    alignas(64) MCPTitleListType base{};
    if (MCP_GetTitleInfo(handle, install.sku->baseTitleId, &base) >= 0 &&
        base.titleId == install.sku->baseTitleId && deviceFromTitle(base) == Device::Disc) {
        install.base.present = true;
        install.base.device = Device::Disc;
        install.base.titleVersion = base.titleVersion;
    }
    MCP_Close(handle);
}

bool readSelectedInstallVersion(GameInstall& install, std::string& error,
                                bool allowRecoveryWrites) {
    install.modVersion = "Not installed";
    const std::string path = install.update.root + "/" + config::VersionRelativePath;
    if (allowRecoveryWrites && !recoverAtomicTextFile(path)) {
        error = "Could not recover the installed version marker in " + install.update.root;
        return false;
    }
    std::string readable = path;
    struct stat information{};
    if (stat(path.c_str(), &information) != 0) {
        if (errno != ENOENT) {
            error = "Could not inspect update.txt in " + install.update.root;
            return false;
        }
        if (allowRecoveryWrites) return true;
        readable = path + ".unbound-old";
        if (stat(readable.c_str(), &information) != 0) {
            if (errno == ENOENT) return true;
            error = "Could not inspect the previous update.txt in " + install.update.root;
            return false;
        }
    }
    if (!S_ISREG(information.st_mode)) {
        error = "The installed update.txt is not a readable file in " + install.update.root;
        return false;
    }
    std::string version;
    if (!readTextFile(readable, version, 65536)) {
        error = "Could not read update.txt in " + install.update.root;
        return false;
    }
    version = coreMarkerVersion(version);
    if (!version.empty()) install.modVersion = version;
    return true;
}

bool revalidateInstallDestination(const GameInstall& install, std::string& error,
                                  bool checkVersionMarker = false) {
    if (!install.destinationReady || !install.update.present || !install.sku ||
        install.sku->game != config::activeGame ||
        (install.update.device != Device::MLC && install.update.device != Device::USB) ||
        install.update.root != stockTitleRoot(install.update.device, install.sku->updateTitleId)) {
        error = install.destinationWarning.empty() ?
            "Choose a verified install destination before changing game files." :
            install.destinationWarning;
        return false;
    }
    InstallLocation current;
    if (probeStockTitle(install.update.device, install.sku->updateTitleId,
                        current, error) != TitleProbe::Valid) {
        if (error.empty()) error = "The selected update folder was removed or moved. Open Install settings and rescan.";
        return false;
    }
    if (!supportedLanguageFolder(install.languageFolder)) {
        error = "The selected language is invalid. Open Install settings and choose an existing folder.";
        return false;
    }
    struct stat languageInfo{};
    const std::string languagePath = current.root + "/content/" + install.languageFolder;
    if (stat(languagePath.c_str(), &languageInfo) != 0 || !S_ISDIR(languageInfo.st_mode)) {
        error = "The selected language folder is no longer available: " + languagePath +
            ". Open Install settings and rescan.";
        return false;
    }
    if (checkVersionMarker) {
        GameInstall verified = install;
        verified.update = current;
        if (!readSelectedInstallVersion(verified, error, false)) return false;
        const std::string previous = trim(install.modVersion);
        const std::string expected = previous.empty() ? "Not installed" : previous;
        if (verified.modVersion != expected) {
            error = "The selected update.txt changed while the package was downloading. "
                "Open Install settings and rescan before installing; no files were changed.";
            return false;
        }
    }
    return true;
}

size_t appendString(char* data, size_t size, size_t count, void* user) {
    auto* output = static_cast<std::string*>(user);
    const auto bytes = size * count;
    if (output->size() + bytes > config::MaxManifestBytes) return 0;
    output->append(data, bytes);
    return bytes;
}

std::string dataAmount(unsigned long long bytes) {
    constexpr double Kilo = 1024.0;
    constexpr double Mega = Kilo * 1024.0;
    constexpr double Giga = Mega * 1024.0;
    char value[40]{};
    if (bytes >= static_cast<unsigned long long>(Giga))
        std::snprintf(value, sizeof(value), "%.2f GB", static_cast<double>(bytes) / Giga);
    else if (bytes >= static_cast<unsigned long long>(Mega))
        std::snprintf(value, sizeof(value), "%.1f MB", static_cast<double>(bytes) / Mega);
    else if (bytes >= static_cast<unsigned long long>(Kilo))
        std::snprintf(value, sizeof(value), "%.0f KB", static_cast<double>(bytes) / Kilo);
    else
        std::snprintf(value, sizeof(value), "%llu B", bytes);
    return value;
}

std::string leafName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    std::string result = slash == std::string::npos ? path : path.substr(slash + 1);
    constexpr std::size_t MaximumShown = 52;
    if (result.size() > MaximumShown)
        result = result.substr(0, 24) + "..." + result.substr(result.size() - 25);
    return result;
}

struct EtaState {
    bool started = false;
    bool rendered = false;
    unsigned nonzeroSamples = 0;
    unsigned long long lastObserved = 0;
    unsigned long long sampleCompleted = 0;
    double smoothedRate = 0.0;
    std::uint32_t firstDataTick = 0;
    std::uint32_t lastAdvanceTick = 0;
    std::uint32_t lastSampleTick = 0;
    std::uint32_t lastRenderTick = 0;
    int lastRenderedPercent = -2;

    void reset() { *this = EtaState{}; }

    void observe(unsigned long long completed, std::uint32_t now) {
        if (started && completed < lastObserved) reset();
        if (!started) {
            lastObserved = completed;
            if (!completed) return;
            started = true;
            firstDataTick = now;
            lastAdvanceTick = now;
            lastSampleTick = now;
            sampleCompleted = completed;
            return;
        }
        if (completed > lastObserved) lastAdvanceTick = now;
        lastObserved = completed;
        const std::uint32_t elapsed = now - lastSampleTick;
        if (elapsed >= 500) {
            const auto delta = completed >= sampleCompleted ? completed - sampleCompleted : 0;
            smoothedRate = updateSmoothedRate(smoothedRate, delta, elapsed);
            if (delta) ++nonzeroSamples;
            sampleCompleted = completed;
            lastSampleTick = now;
        }
    }

    bool shouldRender(int percent, std::uint32_t now, bool force = false) {
        if (force && rendered && percent == lastRenderedPercent) return false;
        if (force || !rendered) {
            rendered = true;
            lastRenderTick = now;
            lastRenderedPercent = percent;
            return true;
        }
        const auto sinceRender = now - lastRenderTick;
        if ((percent != lastRenderedPercent && sinceRender >= 250) || sinceRender >= 1000) {
            lastRenderTick = now;
            lastRenderedPercent = percent;
            return true;
        }
        return false;
    }

    std::string remaining(unsigned long long completed, unsigned long long total,
                          std::uint32_t now) const {
        if (total && completed >= total) return "Finishing...";
        if (!total) return "Time remaining: unavailable";
        if (!started || nonzeroSamples < 2 || now - firstDataTick < 1500)
            return "Calculating time remaining...";
        if (now - lastAdvanceTick >= 3000) return "Waiting for data...";
        unsigned long long seconds = 0;
        if (!estimateRemainingSeconds(completed, total, smoothedRate, seconds))
            return "Calculating time remaining...";
        return formatRemainingTime(seconds);
    }
};

struct FileDownload {
    FILE* file;
    unsigned long long maximum;
    unsigned long long received = 0;
    bool limitExceeded = false;
    Sha256* hash = nullptr;
    unsigned long long accepted = 0;
    DownloadPipe* pipe = nullptr;
    bool writeFailed = false;
    bool usedPipeline = false;
    std::uint64_t writeTicks = 0, hashTicks = 0, curlTicks = 0, queueWaitTicks = 0;
    std::uint64_t networkBytes = 0;
    ResumeResponse* resumeResponse = nullptr;
    DownloadControl* control = nullptr;
};

std::string resumeRequestKey(const std::string& url,const std::string& expected) {
    Sha256 identity;
    identity.update(url.data(),url.size());
    identity.update("\n",1);
    identity.update(expected.data(),expected.size());
    return identity.finish();
}

void removeResumeRecord(const std::string& partial) {
    const std::string path=partial+".resume";
    std::remove(path.c_str());
    std::remove((path+".unbound-new").c_str());
    std::remove((path+".unbound-old").c_str());
}

bool readResumePair(const std::string& partial,unsigned long long maximum,ResumeRecord& record) {
    std::string saved;
    if(!readTextFile(partial+".resume",saved,1024)||!parseResumeRecord(saved,maximum,record))return false;
    struct stat info{};
    return stat(partial.c_str(),&info)==0&&S_ISREG(info.st_mode)&&info.st_size>0&&
        static_cast<unsigned long long>(info.st_size)==record.bytes;
}

bool hashResumePrefix(const std::string& partial,const ResumeRecord& record,Sha256& hash,
                      const ProgressCallback& progress, DownloadControl* control) {
    const BackgroundTickCallback poll = control && control->hasInputPoll()
        ? BackgroundTickCallback{[control] { control->pollInput(); }} : BackgroundTickCallback{};
    return runBackgroundOperation("HQ resume verification",progress,
        [&](const ProgressCallback& relay) {
            AlignedIoBuffer buffer;
            if(!buffer)return false;
            FILE* input=std::fopen(partial.c_str(),"rb");
            if(!input)return false;
            (void)std::setvbuf(input,buffer.data(),_IOFBF,buffer.size());
            AlignedIoBuffer transfer;
            if(!transfer){std::fclose(input);return false;}
            unsigned long long hashed=0;
            std::uint32_t lastUpdate=0;
            bool ok=true;
            hash=Sha256{};
            while(hashed<record.bytes){
                if(downloadCancellationRequested(control)){ok=false;break;}
                const auto requested=static_cast<std::size_t>(std::min<unsigned long long>(transfer.size(),record.bytes-hashed));
                const auto count=std::fread(transfer.data(),1,requested,input);
                if(count!=requested){ok=false;break;}
                hash.update(transfer.data(),count);hashed+=count;
                const auto now=SDL_GetTicks();
                if(relay&&(now-lastUpdate>=250||hashed==record.bytes)){
                    relay("Checking saved download","Verifying "+dataAmount(hashed)+" of "+dataAmount(record.bytes)+" before resuming...",
                          static_cast<int>(hashed*100ULL/record.bytes));
                    lastUpdate=now;
                }
            }
            if(std::fgetc(input)!=EOF||std::ferror(input))ok=false;
            if(std::fclose(input)!=0)ok=false;
            Sha256 snapshot=hash;
            return ok&&snapshot.finish()==record.prefixSha256;
        }, nullptr, poll);
}

size_t receiveResumeHeader(char* data,size_t size,size_t count,void* opaque) {
    if(size&&count>std::numeric_limits<std::size_t>::max()/size)return 0;
    const auto bytes=size*count;
    auto& response=*static_cast<ResumeResponse*>(opaque);
    return response.consume(std::string_view(data,bytes))?bytes:0;
}

bool resumableNetworkFailure(CURLcode result) {
    return result==CURLE_PARTIAL_FILE||result==CURLE_OPERATION_TIMEDOUT||
        result==CURLE_RECV_ERROR||result==CURLE_SEND_ERROR||
        result==CURLE_COULDNT_CONNECT||result==CURLE_COULDNT_RESOLVE_HOST||
        result==CURLE_COULDNT_RESOLVE_PROXY||result==CURLE_GOT_NOTHING;
}

bool writeDownloadChunk(void* opaque, const char* data, std::size_t bytes) {
    auto& download = *static_cast<FileDownload*>(opaque);
    const auto writeBegan = operationMetricsNow();
    const auto written = std::fwrite(data, 1, bytes, download.file);
    download.writeTicks += operationMetricsNow() - writeBegan;
    if (download.hash && written) {
        const auto hashBegan = operationMetricsNow();
        download.hash->update(data, written);
        download.hashTicks += operationMetricsNow() - hashBegan;
    }
    download.received += written;
    if (written != bytes) download.writeFailed = true;
    return written == bytes;
}

size_t appendFile(char* data, size_t size, size_t count, void* user) {
    auto& download = *static_cast<FileDownload*>(user);
    if(downloadCancellationRequested(download.control))return 0;
    if(download.resumeResponse&&!download.resumeResponse->bodyAllowed())return 0;
    std::size_t bytes = 0;
    if (!checkedDownloadChunk(size, count, download.accepted, download.maximum, bytes)) {
        download.limitExceeded = true; return 0;
    }
    const bool accepted = download.pipe ? download.pipe->push(data, bytes)
                                       : writeDownloadChunk(&download, data, bytes);
    if (!accepted) return 0;
    download.accepted += bytes;
    return bytes;
}

struct TransferProgress {
    const ProgressCallback* callback;
    unsigned long long fallbackTotal = 0;
    EtaState eta;
    unsigned long long resumeOffset = 0;
    DownloadControl* control = nullptr;
    bool pollInsideReport = true; // Synchronous fallback only; caller loop polls directly.
};

int reportTransfer(void* opaque, curl_off_t total, curl_off_t current, curl_off_t, curl_off_t) {
    auto& state = *static_cast<TransferProgress*>(opaque);
    if (state.control && state.pollInsideReport) state.control->pollInput();
    if(downloadCancellationRequested(state.control))return 1;
    if (!state.callback || !*state.callback) return 0;
    const auto incoming = current > 0 ? static_cast<unsigned long long>(current) : 0ULL;
    const auto downloaded = state.resumeOffset + incoming;
    unsigned long long expected = total > 0
        ? state.resumeOffset + static_cast<unsigned long long>(total) : state.fallbackTotal;
    if (expected < downloaded) expected = 0;
    const int percent = expected
        ? std::clamp(static_cast<int>((downloaded * 100ULL) / expected), 0, 100) : -1;
    const std::uint32_t now = SDL_GetTicks();
    state.eta.observe(downloaded, now);
    const bool complete = expected && downloaded >= expected;
    if (!state.eta.shouldRender(percent, now, complete)) return 0;
    std::string detail = state.resumeOffset?"Resuming: "+dataAmount(downloaded):dataAmount(downloaded);
    if (expected) detail += " of " + dataAmount(expected);
    else detail += " downloaded";
    detail += "\n" + state.eta.remaining(downloaded, expected, now);
    const auto rate = measuredDownloadRate(state.eta.smoothedRate);
    const bool advancing = !state.eta.started || now - state.eta.lastAdvanceTick < 3000;
    if (!rate.empty() && state.eta.nonzeroSamples >= 2 && advancing) detail += "  |  " + rate;
    (*state.callback)("Downloading package", detail, percent);
    return 0;
}

struct BackgroundTransfer {
    CURL* curl = nullptr;
    FileDownload* download = nullptr;
    SDL_mutex* mutex = nullptr;
    SDL_atomic_t finished{};
    curl_off_t total = 0;
    curl_off_t current = 0;
    CURLcode result = CURLE_OK;
};

int captureTransferProgress(void* opaque, curl_off_t total, curl_off_t current,
                            curl_off_t, curl_off_t) {
    auto& transfer = *static_cast<BackgroundTransfer*>(opaque);
    if(downloadCancellationRequested(transfer.download->control))return 1;
    // A slow screen frame must never make the network wait for a progress lock.
    if (SDL_TryLockMutex(transfer.mutex) == 0) {
        transfer.total = total;
        transfer.current = current;
        SDL_UnlockMutex(transfer.mutex);
    }
    return 0;
}

CURLcode performAndDrain(CURL* curl, FileDownload& download) {
    const auto began = operationMetricsNow();
    const auto result = curl_easy_perform(curl);
    download.curlTicks += operationMetricsNow() - began;
    curl_off_t networkBytes=0;
    if(curl_easy_getinfo(curl,CURLINFO_SIZE_DOWNLOAD_T,&networkBytes)==CURLE_OK&&networkBytes>0)
        download.networkBytes+=static_cast<std::uint64_t>(networkBytes);
    bool writerOk = true;
    if (download.pipe) {
        // Cancellation stops accepting network bytes but drains the bytes that
        // were already queued. The saved hash and prefix then agree exactly.
        const bool drainAccepted = result == CURLE_OK ||
            downloadCancellationRequested(download.control);
        writerOk = download.pipe->finish(drainAccepted);
        // finish(false) intentionally discards queued bytes on a network
        // failure; it is not itself an SD write failure. Preserve resume there.
        if (drainAccepted && !writerOk) download.writeFailed = true;
        download.queueWaitTicks += download.pipe->waitTicks();
    }
    return result == CURLE_OK && !writerOk ? CURLE_WRITE_ERROR : result;
}

int runBackgroundTransfer(void* opaque) {
    auto& transfer = *static_cast<BackgroundTransfer*>(opaque);
    transfer.result = performAndDrain(transfer.curl, *transfer.download);
    SDL_AtomicSet(&transfer.finished, 1);
    return 0;
}

CURLcode performFileTransfer(CURL* curl, TransferProgress& progress, FileDownload& download) {
    DownloadPipe pipe;
    if (pipe.start(writeDownloadChunk, &download)) {
        download.pipe = &pipe;
        download.usedPipeline = true;
    }
    if (!progress.callback || !*progress.callback) {
        const auto result = performAndDrain(curl, download);
        download.pipe = nullptr;
        return result;
    }

    BackgroundTransfer transfer;
    transfer.curl = curl;
    transfer.download = &download;
    transfer.mutex = SDL_CreateMutex();
    SDL_Thread* worker = nullptr;
    if (transfer.mutex) {
        SDL_AtomicSet(&transfer.finished, 0);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, captureTransferProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
        // Wii U SDL defaults to a 32 KiB stack, too small for HTTPS parsing.
        worker = SDL_CreateThreadWithStackSize(runBackgroundTransfer, "HQ download",
                                               256 * 1024, &transfer);
    }
    if (!worker) {
        if (transfer.mutex) SDL_DestroyMutex(transfer.mutex);
        // Allocation failure retains the proven synchronous transfer path.
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportTransfer);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
        const auto result = performAndDrain(curl, download);
        download.pipe = nullptr;
        return result;
    }

    progress.pollInsideReport = false;
    while (!SDL_AtomicGet(&transfer.finished)) {
        if (progress.control) progress.control->pollInput();
        curl_off_t total = 0, current = 0;
        SDL_LockMutex(transfer.mutex);
        total = transfer.total;
        current = transfer.current;
        SDL_UnlockMutex(transfer.mutex);
        reportTransfer(&progress, total, current, 0, 0);
        SDL_Delay(25);
    }
    // All FILE/hash/CURL state is owned by the worker until this join completes.
    SDL_WaitThread(worker, nullptr);
    download.pipe = nullptr;
    curl_off_t total = transfer.total, current = transfer.current;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &current);
    if (progress.control) progress.control->pollInput();
    reportTransfer(&progress, total, current, 0, 0);
    progress.pollInsideReport = true;
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportTransfer);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    SDL_DestroyMutex(transfer.mutex);
    return transfer.result;
}

int tuneDownloadSocket(void*, curl_socket_t socket, curlsocktype purpose) {
    if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKOPT_OK;
    const int previousErrno = errno;
    const int enabled = 1;
    const int receiveBytes = 128 * 1024;
    (void)setsockopt(socket, SOL_SOCKET, SO_WINSCALE, &enabled, sizeof(enabled));
    (void)setsockopt(socket, SOL_SOCKET, SO_TCPSACK, &enabled, sizeof(enabled));
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &receiveBytes, sizeof(receiveBytes));
    // Unsupported performance options must not prevent a normal download.
    errno = previousErrno;
    return CURL_SOCKOPT_OK;
}

bool configureCurl(CURL* curl, const std::string& url, std::string& error) {
    if (url.rfind("https://", 0) != 0) { error = "Only HTTPS download URLs are allowed"; return false; }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 256L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128L * 1024L);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, tuneDownloadSocket);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BO2-Unbound-HQ/0.1 WiiU");
    // The Wii U network stack is IPv4-only in normal retail configurations.
    // Avoid an IPv6 lookup/connection path that can report CURLE_COULDNT_RESOLVE_HOST.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    return configureHttpsTrust(curl, error);
}

bool curlResult(CURL* curl, CURLcode result, const char* detail, std::string& error) {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK) {
        error = (detail && *detail) ? detail : curl_easy_strerror(result);
        error += " (curl " + std::to_string(static_cast<int>(result)) + ")";
        return false;
    }
    if (status < 200 || status >= 300) { error = "Server returned HTTP " + std::to_string(status); return false; }
    return true;
}

struct DownloadProbeProgress {
    const ProgressCallback* callback = nullptr;
    DownloadControl* control = nullptr;
    std::uint32_t lastRenderTick = 0;
    bool rendered = false;
};

int reportDownloadProbe(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto& state = *static_cast<DownloadProbeProgress*>(opaque);
    if (downloadCancellationRequested(state.control)) return 1;
    const auto now = SDL_GetTicks();
    if (state.callback && *state.callback &&
        (!state.rendered || now - state.lastRenderTick >= 250)) {
        state.rendered = true;
        state.lastRenderTick = now;
        (*state.callback)("Preparing download", "Checking package size and connecting to the server...", -1);
    }
    return 0;
}

bool remoteFileSize(CURL* curl, const std::string& url, unsigned long long& size,
                    const ProgressCallback& progress, DownloadControl* control) {
    if (!curl) return false;
    const BackgroundTickCallback poll = control && control->hasInputPoll()
        ? BackgroundTickCallback{[control] { control->pollInput(); }} : BackgroundTickCallback{};
    return runBackgroundOperation("HQ package size", progress,
        [&](const ProgressCallback& relay) {
            if (downloadCancellationRequested(control)) return false;
            curl_easy_reset(curl);
            std::string error;
            if (!configureCurl(curl, url, error)) return false;
            char curlError[CURL_ERROR_SIZE]{};
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            DownloadProbeProgress probe{&relay, control};
            if (relay || control) {
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportDownloadProbe);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &probe);
            }
            const auto result = curl_easy_perform(curl);
            curl_off_t length = -1;
            const bool ok = !downloadCancellationRequested(control) &&
                curlResult(curl, result, curlError, error) &&
                curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length) == CURLE_OK &&
                length >= 0;
            if (ok) size = static_cast<unsigned long long>(length);
            return ok;
        }, nullptr, poll);
}

struct InstallProgress {
    const ProgressCallback* callback = nullptr;
    unsigned long long total = 0;
    unsigned long long completed = 0;
    EtaState eta;

    void report(const std::string& phase, unsigned long long added = 0) {
        if (total) {
            const auto room = completed < total ? total - completed : 0;
            completed += std::min(room, added);
        } else {
            completed += added;
        }
        if (!callback || !*callback) return;
        const std::uint32_t now = SDL_GetTicks();
        eta.observe(completed, now);
        const int percent = total
            ? std::clamp(static_cast<int>((completed * 100ULL) / total), 0, 100) : -1;
        if (!eta.shouldRender(percent, now)) return;
        std::string detail = phase + "\n" + eta.remaining(completed, total, now);
        if (total) detail += "  |  " + dataAmount(completed) + " of " + dataAmount(total);
        (*callback)("Unpacking and installing", detail, percent);
    }

    void finishing(const std::string& detail) {
        if (callback && *callback)
            (*callback)("Finishing installation", detail + "\nPlease wait. Do not power off the console.", 100);
    }

    void finish() {
        completed = total;
        if (callback && *callback)
            (*callback)("Installation complete", "All package files were written successfully.", 100);
    }
};

bool closeInstallOutput(FILE* output, OperationMetrics& metrics) {
    const auto started = operationMetricsNow();
    const bool ok = std::fclose(output) == 0;
    metrics.closeTicks += operationMetricsNow() - started;
    return ok;
}

bool extractCurrent(zip_t* zip, zip_uint64_t index, const std::string& displayName,
                    const std::string& target, InstallProgress& installProgress,
                    FileIoContext& io, unsigned long long expectedBytes,
                    OperationMetrics& metrics,
                    std::string& error) {
    const std::string shownName = leafName(displayName);
    if (!io.prepareDirectory(parentPath(target))) { error = io.directoryError(); return false; }
    // Resolve only this file's brief rename-swap state. No game files are copied
    // to SD and already committed package files are not rolled back on failure.
    if (!recoverInstallReplacement(target, error)) return false;
    const std::string temporary = target + ".unbound-new";
    StdioInstallFileOps replacementFiles;
    const auto temporaryState = replacementFiles.inspect(temporary);
    if (temporaryState == InstallPathState::Other || temporaryState == InstallPathState::Error) {
        error = "A temporary replacement path is not an accessible regular file: " + temporary;
        return false;
    }
    if (temporaryState == InstallPathState::RegularFile && !replacementFiles.removeFile(temporary)) {
        error = "Could not clear the previous temporary replacement: " + temporary;
        return false;
    }
    FILE* output = std::fopen(temporary.c_str(), "wb");
    if (!output) { error = "Could not create " + temporary; return false; }
    if (std::setvbuf(output, io.output.data(), _IOFBF, io.output.size()) != 0) {
        closeInstallOutput(output, metrics);
        std::remove(temporary.c_str());
        error = "Could not configure ZIP output buffering";
        return false;
    }
    const auto openedAt = operationMetricsNow();
    zip_file_t* entry = zip_fopen_index(zip, index, ZIP_FL_UNCHANGED);
    metrics.archiveReadTicks += operationMetricsNow() - openedAt;
    if (!entry) {
        closeInstallOutput(output, metrics);
        std::remove(temporary.c_str());
        error = "Could not open ZIP entry";
        return false;
    }
    const std::string unpackPhase = "Unpacking " + shownName + "...";
    installProgress.report(unpackPhase);
    const auto streamed = streamInstallEntry(expectedBytes, io.transfer.data(), io.transfer.size(),
        [entry](char* data, std::size_t size) { return zip_fread(entry, data, size); },
        [output](const char* data, std::size_t size) { return std::fwrite(data, 1, size, output); },
        [&](std::size_t size) { installProgress.report(unpackPhase, size); }, metrics);
    bool ok = streamed == InstallStreamResult::Success;
    const auto closedAt = operationMetricsNow();
    if (zip_fclose(entry) != 0) ok = false;
    metrics.archiveReadTicks += operationMetricsNow() - closedAt;
    if (!closeInstallOutput(output, metrics)) ok = false;
    if (!ok) { std::remove(temporary.c_str()); error = "Failed writing ZIP entry"; return false; }
    // Only a fully closed entry with matching size/CRC can replace its target.
    // Preserve the verified temporary file if the rename fails, for inspection
    // or a later retry. A successful swap retains no previous-file backup.
    return commitInstallReplacement(temporary, target, error);
}

using Mapper = bool (*)(const std::string&, std::string&, std::string&, void*);
using InstallCommit = std::function<bool(std::string&)>;
bool coreMapper(const std::string&, std::string&, std::string&, void*);

struct ArchiveEntry {
    zip_uint64_t index;
    std::string name;
    std::string target;
    std::string logical;
    unsigned long long bytes;
    bool directory;
    bool consoleVideo = false;
};

#include "native_inherited_content.hpp"

bool isMovieContentPath(const GameInstall& install, const std::string& path) {
    if (path.rfind(install.update.root + "/content/", 0) != 0) return false;
    return routeMovieInstallPath("update" + path.substr(install.update.root.size()),
                                 install.languageFolder).status == MovieInstallPathStatus::Movie;
}

// The game already searches <language>/video before the shared video folder.
// Create only those normal localized destinations. Never convert their parents
// or grant a cache exception for a native link/reference.
bool prepareLocalizedMovieDirectories(const GameInstall& install,
                                      const std::vector<ArchiveEntry>& entries,
                                      FileIoContext& folders, std::string& error,
                                      const ProgressCallback& progress) {
    std::set<std::string> directories;
    for (const auto& entry : entries)
        if (entry.consoleVideo)
            directories.insert(entry.directory ? entry.target : parentPath(entry.target));
    if (directories.empty()) return true;
    std::string report = "HQ " + std::string(config::AppVersion) + " localized movie installation\n"
        "Strategy: existing native localized-movie lookup; normal console installation.\n"
        "Shared content/video reference is not opened, renamed, converted, or copied.\n"
        "Selected language: " + install.languageFolder + "\n";
    const auto finish = [&](bool ok) {
        report += std::string("Directory preparation: ") + (ok ? "ok\n" : "failed\n");
        if (!ok) report += error + "\n";
        std::string logError;
        // Diagnostic logging never authorizes an install or changes its result.
        writeTextFileAtomic(std::string(config::SdRoot) + "/native-video-check.txt", report, logError);
        return ok;
    };
    if (!validatedInheritedTarget(install, error)) return finish(false);
    struct MovieDirectoryIo : PosixDirectoryIo {
        DirectoryStatResult inspect(const std::string& path) {
            struct stat info{};
            errno = 0;
            if (lstat(path.c_str(), &info) == 0)
                return {0, static_cast<std::uint32_t>(info.st_mode)};
            return {errno ? errno : EIO, 0};
        }
    } io;
    const DirectoryActivity activity = [&](const char* operation, const std::string& path) {
        if (progress) progress("Preparing movie files", "Checking the localized movie destination...", -1);
        report += std::string(operation) + ": " + path + "\n";
    };
    for (const auto& directory : directories) {
        report += "Destination: " + directory + "\n";
        if (!prepareDirectoryPrefixes(directory, directoryPrefixes(directory), folders.directories,
                                      io, folders.directoryFailure, activity)) {
            error = folders.directoryError();
            return finish(false);
        }
    }
    return finish(true);
}

struct CoreContentFolderLinkIo {
    const GameInstall& install;
    const DirectoryActivity& activity;
    const std::string& relativeContentPath;
    NativeInheritedBatch& batch;
    bool allowRecoveryWrites=true;

    FolderLinkState inspect(const std::string& path) {
        struct stat state{};
        errno = 0;
        if (lstat(path.c_str(), &state) != 0)
            return {errno == ENOENT ? FolderLinkEntry::Missing : FolderLinkEntry::Error,
                    errno ? errno : EIO};
        if (S_ISLNK(state.st_mode)) return {FolderLinkEntry::Link, 0};
        if (S_ISDIR(state.st_mode)) return {FolderLinkEntry::Directory, 0};
        return {FolderLinkEntry::Other, 0};
    }
    int rename(const std::string& from, const std::string& to) {
        errno = 0;
        return std::rename(from.c_str(), to.c_str()) == 0 ? 0 : (errno ? errno : EIO);
    }
    int makeDirectory(const std::string& path) {
        errno = 0;
        return mkdir(path.c_str(), 0777) == 0 ? 0 : (errno ? errno : EIO);
    }
    int checkEmptyDirectory(const std::string& path) {
        errno = 0;
        DIR* folder = opendir(path.c_str());
        if (!folder) return errno ? errno : EIO;
        bool empty = true;
        errno = 0;
        while (const auto* item = readdir(folder)) {
            if (std::strcmp(item->d_name, ".") && std::strcmp(item->d_name, "..")) {
                empty = false;
                break;
            }
        }
        const int readError = errno;
        errno = 0;
        const int closeError = closedir(folder) == 0 ? 0 : (errno ? errno : EIO);
        return readError ? readError : (closeError ? closeError : (empty ? 0 : ENOTEMPTY));
    }
    FolderLinkOwnership readOwnership(const FolderLinkPaths& paths, std::string& error) {
        const auto read = [&](const std::string& path) {
            if (activity) activity("check folder repair record", path);
            struct stat state{};
            errno = 0;
            if (lstat(path.c_str(), &state) != 0) {
                if (errno == ENOENT) return FolderLinkOwnership::Missing;
                error = "Could not inspect the update folder repair record: " + path +
                        "; errno=" + std::to_string(errno) + ".";
                return FolderLinkOwnership::Invalid;
            }
            std::string text;
            const auto otherDevice=install.update.device==Device::USB?Device::MLC:Device::USB;
            const auto alternate=stockTitleRoot(otherDevice,install.sku->updateTitleId)+
                paths.target.substr(install.update.root.size());
            const bool linkGuard=inspect(paths.original()).kind==FolderLinkEntry::Link||
                inspect(paths.target).kind==FolderLinkEntry::Link;
            if (!S_ISREG(state.st_mode) || state.st_size <= 0 || state.st_size > 1024 ||
                !readTextFile(path, text, 1024) ||
                !inheritedOwnershipTextMatches(text,paths.target,alternate,linkGuard)) {
                error = "The update folder repair record is unreadable or does not match this destination. "
                        "It was retained: " + path;
                return FolderLinkOwnership::Invalid;
            }
            return FolderLinkOwnership::Valid;
        };
        const auto primary = read(paths.journal());
        if (primary == FolderLinkOwnership::Invalid) return primary;
        const auto staged = read(paths.journal() + ".unbound-new");
        if (staged == FolderLinkOwnership::Invalid) return staged;
        const auto old = read(paths.journal() + ".unbound-old");
        if (old != FolderLinkOwnership::Missing) {
            if (error.empty()) error = "An unexpected previous update folder repair record was retained: " + paths.journal();
            return FolderLinkOwnership::Invalid;
        }
        if (primary == FolderLinkOwnership::Valid) return primary;
        if (staged == FolderLinkOwnership::Missing) return FolderLinkOwnership::Missing;
        if(!allowRecoveryWrites)return FolderLinkOwnership::Valid;
        if (activity) activity("recover folder repair record", paths.journal());
        const int restored = rename(paths.journal() + ".unbound-new", paths.journal());
        if (restored) {
            error = "Could not restore the update folder repair record; errno=" + std::to_string(restored) + ".";
            return FolderLinkOwnership::Invalid;
        }
        return FolderLinkOwnership::Valid;
    }
    bool writeOwnership(const FolderLinkPaths& paths, std::string& error) {
        // This record is written only once. A complete, exact existing record
        // is never overwritten, and its retained opaque link is never removed.
        return writeTextFileAtomic(paths.journal(), paths.ownershipText(), error);
    }
    bool validateOriginalContent(std::string& error) {
        if(!batch.source(error))return false;
        const auto source=batch.sourceContent+"/"+relativeContentPath;
        if(batch.trees.inspect(source).kind!=InheritedEntryKind::Directory){
            error="The matching original content folder is unavailable: "+source;return false;
        }
        return true;
    }
    bool preflightInheritedDirectory(const FolderLinkPaths& paths,bool live,std::string& error){
        return batch.plan(paths,live,error)&&batch.checkSpace(error);
    }
    bool populateInheritedDirectory(const FolderLinkPaths& paths,bool,std::string& error){
        const auto found=batch.plans.find(paths.target);
        if(found==batch.plans.end()){error="Original-content preservation was not preflighted.";return false;}
        return executeInheritedTreePlan(found->second,batch.trees,error);
    }
    bool markInheritedComplete(const FolderLinkPaths& paths,std::string& error){
        if(inspect(paths.target).kind!=FolderLinkEntry::Directory||inspect(paths.original()).kind!=FolderLinkEntry::Link){
            error="The repaired directory/reference state could not be verified.";return false;
        }
        InheritedTreePlan covered;
        if(!buildInheritedTreePlan(batch.sourceContent+"/"+relativeContentPath,paths.target,
            InheritedCopyMode::RepairMissing,batch.trees,covered,error))return false;
        if(covered.copyBytes){error="Original-content child coverage is still incomplete: "+paths.target;return false;}
        batch.plans[paths.target]=covered;
        const auto receipt=paths.target+".unbound-link-complete";
        for(const std::string ending:{"",".unbound-new",".unbound-old"}){
            const auto state=batch.trees.inspect(receipt+ending);
            if(state.kind!=InheritedEntryKind::Missing&&state.kind!=InheritedEntryKind::Regular){
                error="An unexpected completion-record entry was retained: "+receipt+ending;return false;
            }
        }
        const auto completed=batch.completion(paths,covered);
        std::string existing;
        if(batch.trees.inspect(receipt).kind==InheritedEntryKind::Regular&&
           readTextFile(receipt,existing,2048)&&existing==completed)return true;
        return writeTextFileAtomic(receipt,completed,error);
    }
};

bool discoverInheritedOwners(const GameInstall& install,std::set<std::string>& owners,
                             std::string& error,const ProgressCallback& progress){
    owners.clear();
    const auto root=install.update.root+"/content";
    NativeInheritedProgress status{progress,0,0,{}};
    NativeInheritedTreeIo io(status);
    if(io.inspect(root).kind!=InheritedEntryKind::Directory){error="The update content root is unavailable for repair discovery.";return false;}
    std::vector<std::pair<std::string,unsigned>> pending{{root,0}};
    std::size_t entries=0;
    for(std::size_t pos=0;pos<pending.size();++pos){
        const auto current=pending[pos];
        if(current.second>20||pending.size()>4096){error="The inherited-content discovery limit was reached.";return false;}
        if(progress)progress("Checking original game files","Checking converted folders for required stock-file repair...",-1);
        std::vector<std::string> names;
        if(!io.listNames(current.first,names,error))return false;
        for(const auto& name:names){
            if(++entries>65536){error="The inherited-content discovery entry limit was reached.";return false;}
            if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:")!=std::string::npos){error="An invalid name was found during inherited-content discovery.";return false;}
            const auto path=current.first+"/"+name;
            const auto reserved=name.find(".unbound-link-");
            if(reserved!=std::string::npos){
                const auto ending=name.substr(reserved);
                if(ending==".unbound-link-original"||ending==".unbound-link-new"||ending==".unbound-link-state"||
                   ending==".unbound-link-state.unbound-new"||ending==".unbound-link-state.unbound-old"||
                   ending==".unbound-link-complete"||ending==".unbound-link-complete.unbound-new"||
                   ending==".unbound-link-complete.unbound-old"||ending==".unbound-link-retired"){
                    const auto target=current.first+"/"+name.substr(0,reserved);
                    std::set<std::string> prefixes;
                    if(!addContentFolderLinkPrefixes(root,target,prefixes,error)||!prefixes.count(target)){
                        error="An inherited-content owner path is invalid: "+target;return false;
                    }
                    owners.insert(target);
                }
                continue; // Never traverse retained links or private staging trees.
            }
            const auto state=io.inspect(path);
            if(state.kind==InheritedEntryKind::Error){error="Could not inspect update content while locating repair records: "+path;return false;}
            if(state.kind==InheritedEntryKind::Directory&&!isMovieContentPath(install,path))
                pending.push_back({path,current.second+1});
        }
    }
    return true;
}

bool inspectOwnedInheritedRoot(const GameInstall& install,const std::string& target,
                                NativeInheritedBatch& batch,bool& needed,std::string& error){
    const auto relative=target.substr((install.update.root+"/content/").size());
    const DirectoryActivity activity=[&](const char* action,const std::string& path){
        if(batch.progress)batch.progress("Checking original game files",std::string(action)+": "+path,-1);
    };
    CoreContentFolderLinkIo io{install,activity,relative,batch,false};
    const FolderLinkPaths paths{target};
    if(io.readOwnership(paths,error)!=FolderLinkOwnership::Valid){
        if(error.empty())error="An inherited-content recovery entry has no valid owner record: "+target;
        return false;
    }
    const auto current=io.inspect(target),original=io.inspect(paths.original()),staged=io.inspect(paths.staged());
    const bool live=current.kind==FolderLinkEntry::Directory&&original.kind==FolderLinkEntry::Link&&staged.kind==FolderLinkEntry::Missing;
    const bool fresh=(current.kind==FolderLinkEntry::Link&&original.kind==FolderLinkEntry::Missing&&
        (staged.kind==FolderLinkEntry::Missing||staged.kind==FolderLinkEntry::Directory))||
        (current.kind==FolderLinkEntry::Missing&&original.kind==FolderLinkEntry::Link&&
         (staged.kind==FolderLinkEntry::Missing||staged.kind==FolderLinkEntry::Directory));
    if(!live&&!fresh){error="An inherited-content recovery state is ambiguous and was retained: "+target;return false;}
    if(!batch.plan(paths,live,error))return false;
    bool complete=false;
    if(live&&!batch.complete(paths,complete,error))return false;
    needed=!complete;
    return true;
}

// r35-r37 converted the shared content/video reference, either leaving it empty
// (stock movies hidden) or copying the whole stock library. Since r40 the game's
// localized lookup carries Unbound movies, so the retained reference goes back.
struct RetainedReferenceIo {
    NativeInheritedBatch& batch;
    const ProgressCallback& progress;
    FolderLinkPaths paths;
    std::set<std::string> trusted; // relative stock copies covered by an exact HQ receipt
    std::uint64_t checked=0,total=0;
    InheritedStat inspect(const std::string& path){return batch.trees.inspect(path);}
    bool listNames(const std::string& path,std::vector<std::string>& names,std::string& error){
        return batch.trees.listNames(path,names,error);
    }
    std::vector<std::string> records() const {
        // Owner record last: discovery keeps recognizing an interrupted cleanup.
        return {paths.complete(),paths.complete()+".unbound-new",paths.complete()+".unbound-old",
                paths.journal()+".unbound-old",paths.journal()+".unbound-new",paths.journal()};
    }
    bool recordsPresent(const FolderLinkPaths&){
        for(const auto& path:records())if(inspect(path).kind!=InheritedEntryKind::Missing)return true;
        return false;
    }
    int rename(const std::string& from,const std::string& to){
        errno=0;return std::rename(from.c_str(),to.c_str())==0?0:(errno?errno:EIO);
    }
    int removeFile(const std::string& path){errno=0;return std::remove(path.c_str())==0?0:(errno?errno:EIO);}
    int removeDirectory(const std::string& path){errno=0;return rmdir(path.c_str())==0?0:(errno?errno:EIO);}
    bool prepareDirectory(const std::string& path,std::string& error){
        struct StrictDirectoryIo:PosixDirectoryIo{
            DirectoryStatResult inspect(const std::string& current){
                struct stat info{};errno=0;
                if(lstat(current.c_str(),&info)==0)return {0,static_cast<std::uint32_t>(info.st_mode)};
                return {errno?errno:EIO,0};
            }
        } io;
        std::unordered_set<std::string> verified;DirectoryFailure failure;
        if(prepareDirectoryPrefixes(path,directoryPrefixes(path),verified,io,failure))return true;
        error=failure.message();return false;
    }
    void report(const std::string& file,std::uint64_t bytes){
        checked+=std::min(bytes,total-std::min(checked,total));
        if(progress)progress("Restoring original movie folder","Checking copied file "+leafName(file)+"\nFreed "+
            dataAmount(checked)+(total?" of "+dataAmount(total):""),total?static_cast<int>(std::min<std::uint64_t>(99,checked*100/total)):-1);
    }
    bool sameContents(const std::string& path,const std::string& other,std::uint64_t bytes,bool& same,std::string& error){
        same=false;
        for(const auto& tree:{paths.target,paths.retired()}){
            if(path.rfind(tree+"/",0)==0&&trusted.count(path.substr(tree.size()+1))&&
               other==batch.sourceContent+"/video/"+path.substr(tree.size()+1)){same=true;report(path,bytes);return true;}
        }
        auto& buffers=batch.trees.buffers;
        if(!buffers.ready()){error="Original folder restore: could not allocate comparison buffers.";return false;}
        const auto chunk=std::min(buffers.transfer.size(),buffers.output.size());
        FILE* left=std::fopen(path.c_str(),"rb");
        FILE* right=left?std::fopen(other.c_str(),"rb"):nullptr;
        bool ok=left&&right;same=ok;std::uint64_t compared=0;
        while(ok&&same&&compared<bytes){
            const auto want=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,bytes-compared));
            if(std::fread(buffers.transfer.data(),1,want,left)!=want||std::fread(buffers.output.data(),1,want,right)!=want){ok=false;break;}
            same=std::memcmp(buffers.transfer.data(),buffers.output.data(),want)==0;
            compared+=want;report(path,want);
        }
        if(ok&&same&&(std::fgetc(left)!=EOF||std::fgetc(right)!=EOF))same=false;
        if(left&&std::fclose(left)!=0)ok=false;
        if(right&&std::fclose(right)!=0)ok=false;
        if(!ok){same=false;error="Original folder restore: could not read "+path+" for comparison. Nothing was removed.";}
        return ok;
    }
    bool removeRecords(const FolderLinkPaths&,std::string& error){
        for(const auto& path:records()){
            const auto state=inspect(path);
            if(state.kind==InheritedEntryKind::Missing)continue;
            if(state.kind!=InheritedEntryKind::Regular||std::remove(path.c_str())!=0){
                error="Could not remove the finished movie-folder record: "+path;return false;
            }
        }
        return true;
    }
};

bool planSharedMovieRestore(const GameInstall& install,RetainedReferenceIo& io,
                            RetainedReferencePlan& plan,std::string& error){
    const DirectoryActivity activity=[&](const char* action,const std::string& path){
        if(io.progress)io.progress("Checking original game files",std::string(action)+": "+path,-1);
    };
    const std::string relative="video";
    CoreContentFolderLinkIo owner{install,activity,relative,io.batch,false};
    if(owner.readOwnership(io.paths,error)!=FolderLinkOwnership::Valid){
        if(error.empty())error="A movie-folder recovery entry has no valid owner record: "+io.paths.target;
        return false;
    }
    if(!io.batch.source(error))return false;
    const auto stock=io.batch.sourceContent+"/video";
    std::string relocation;
    const auto route=routeMovieInstallPath("update/content/video",install.languageFolder);
    if(route.status==MovieInstallPathStatus::Movie)
        relocation=install.update.root+route.movieDirectory.substr(std::string("update").size());
    if(!planRetainedReferenceRestore(io.paths,stock,relocation,io,plan,error))return false;
    // r37 copies were verified when made. Trust a copy only while HQ's exact
    // completion receipt still matches BASE and the file is not newer than it.
    if(plan.state==RetainedReferenceState::Converted){
        InheritedTreePlan covered;std::string ignored,text;struct stat receipt{};
        if(buildInheritedTreePlan(stock,io.paths.target,InheritedCopyMode::RepairMissing,io.batch.trees,covered,ignored)&&
           lstat(io.paths.complete().c_str(),&receipt)==0&&S_ISREG(receipt.st_mode)&&
           readTextFile(io.paths.complete(),text,2048)&&text==io.batch.completion(io.paths,covered))
            for(const auto& item:plan.items){
                struct stat copy{};
                if(item.action==RetainedChildAction::RemoveIfIdentical&&item.compareWith==stock+"/"+item.relative&&
                   lstat((io.paths.target+"/"+item.relative).c_str(),&copy)==0&&copy.st_mtime<=receipt.st_mtime)
                    io.trusted.insert(item.relative);
            }
    }
    io.total=plan.reclaimBytes;
    static const char* states[]{"converted","reference set aside","restored","stage set aside","stage left","clean"};
    std::string report="HQ "+std::string(config::AppVersion)+" movie folder restore\n"
        "Folder: "+io.paths.target+"\nState: "+states[static_cast<int>(plan.state)]+
        "\nUnbound/user files moved to: "+(relocation.empty()?std::string("unavailable"):relocation)+
        "\nCopied files to free: "+std::to_string(plan.reclaimBytes)+" bytes ("+std::to_string(io.trusted.size())+
        " covered by HQ's receipt)\nFiles to move: "+std::to_string(plan.relocateBytes)+" bytes\nKept for review: "+
        std::to_string(plan.kept)+(plan.blocked.empty()?std::string{}:"\nSkipped: "+plan.blocked)+"\n";
    std::string ignored;writeTextFileAtomic(std::string(config::SdRoot)+"/movie-folder-restore.txt",report,ignored);
    return true;
}

bool inheritedRepairOperation(const GameInstall& install,InheritedContentRepairInfo& information,
                              std::string& error,const ProgressCallback& progress,bool apply){
    information={};error.clear();
    if(!install.sku||!install.update.present)return true;
    if(!validatedInheritedTarget(install,error)){information.needed=true;return false;}
    std::set<std::string> owners;
    if(!discoverInheritedOwners(install,owners,error,progress)){information.needed=true;return false;}
    const auto sharedMovies=install.update.root+"/content/video";
    bool movieOwner=false;
    for(auto it=owners.begin();it!=owners.end();){
        if(*it==sharedMovies){movieOwner=true;it=owners.erase(it);}
        else if(isMovieContentPath(install,*it))it=owners.erase(it);
        else ++it;
    }
    if(owners.empty()&&!movieOwner)return true;
    information.needed=true;
    NativeInheritedBatch batch(install,progress);
    RetainedReferenceIo movies{batch,progress,FolderLinkPaths{sharedMovies},{},0,0};
    RetainedReferencePlan moviePlan;
    if(movieOwner&&!planSharedMovieRestore(install,movies,moviePlan,error))return false;
    const bool restoreMovies=movieOwner&&moviePlan.needed();
    if(restoreMovies){information.restoreFolders=1;information.reclaimBytes=moviePlan.reclaimBytes;}
    std::set<std::string> required;
    for(const auto& target:owners){bool needed=true;
        if(!inspectOwnedInheritedRoot(install,target,batch,needed,error))return false;
        if(needed)required.insert(target);
    }
    auto unique=batch.uniquePlans(error);if(!error.empty())return false;
    information.needed=!required.empty()||restoreMovies;
    for(const auto& plan:unique){
        if(plan.copyBytes>std::numeric_limits<unsigned long long>::max()-information.copyBytes){error="Inherited copy size overflow.";return false;}
        information.copyBytes+=plan.copyBytes;
        information.files+=plan.files;information.folders+=plan.directories;
    }
    std::uint64_t requiredSpace=0;
    if(!inheritedRequiredSpace(unique,8ULL*1024*1024,requiredSpace,error))return false;
    if(required.empty())requiredSpace=0; // A movie-folder restore only renames and frees space.
    information.requiredBytes=requiredSpace;
    unsigned long long available=0;
    if(!freeSpaceForPath(install.update.root,available)){error="Could not measure free space for original-content repair.";return false;}
    information.freeBytes=available;
    information.summary="Preserve missing original game files locally: "+dataAmount(information.copyBytes)+
        ". No game-package download is required.\nRequired free space: "+dataAmount(requiredSpace)+
        " ("+std::to_string(requiredSpace)+" bytes).\nAvailable: "+dataAmount(available)+" ("+std::to_string(available)+" bytes).";
    if(restoreMovies)information.summary+="\nReturn the original movie folder link. Copied movies to free: "+
        dataAmount(information.reclaimBytes)+". Details: movie-folder-restore.txt.";
    if(!apply||!information.needed)return true;
    // Renames only, then copy removal: this never needs free space and runs
    // before any stock copy so the freed space is already available to it.
    if(restoreMovies&&!executeRetainedReferenceRestore(movies.paths,moviePlan,movies,error))return false;
    if(!required.empty()&&!batch.checkSpace(error))return false;
    const auto contentRoot=install.update.root+"/content";
    for(const auto& target:required){
        const auto relative=target.substr(contentRoot.size()+1);
        const DirectoryActivity activity=[&](const char* action,const std::string& path){
            if(progress)progress("Restoring original game files",std::string(action)+": "+path,-1);
        };
        FileIoContext strictParents;
        if(!strictParents.prepareDirectory(parentPath(target),activity)){
            error=strictParents.directoryError();return false;
        }
        CoreContentFolderLinkIo io{install,activity,relative,batch};
        if(!materializeTitleFolderLink(FolderLinkPaths{target},io,error,activity))return false;
    }
    if(progress)progress("Original game files restored","Stock content is preserved. Existing Unbound files were kept.",100);
    information.needed=false;
    return true;
}

bool prepareCoreContentPatchReferences(const GameInstall& install, const std::vector<ArchiveEntry>& entries,
                                       FileIoContext& folders, std::string& error,
                                       const ProgressCallback& progress, const InstallCommit& beforeMutation) {
    if(!validatedInheritedTarget(install,error))return false;
    const std::string contentRoot=install.update.root+"/content";
    std::set<std::string> candidates;
    std::uint64_t payloadBytes=0;
    for(const auto& entry:entries){
        if(entry.logical=="sd"||entry.logical.rfind("sd/",0)==0)continue;
        // Movie destinations were checked separately as normal directories.
        // Never materialize a stock movie reference or inventory its library.
        if(!entry.consoleVideo&&!addContentFolderLinkPrefixes(contentRoot,entry.directory?entry.target:parentPath(entry.target),candidates,error))return false;
        if(!entry.directory&&!inherited_detail::add(payloadBytes,entry.bytes)){error="Package size overflow.";return false;}
    }
    std::set<std::string> existingOwners;
    if(!discoverInheritedOwners(install,existingOwners,error,progress))return false;
    for(auto it=existingOwners.begin();it!=existingOwners.end();){
        if(isMovieContentPath(install,*it))it=existingOwners.erase(it);
        else ++it;
    }
    candidates.insert(existingOwners.begin(),existingOwners.end());
    if(candidates.empty())return true;
    NativeInheritedBatch batch(install,progress,payloadBytes);
    const DirectoryActivity activity=[&](const char* operation,const std::string& path){
        if(progress)progress("Preparing content folders",std::string(operation)+": "+path,-1);
    };
    const std::string rootRelative;
    CoreContentFolderLinkIo rootIo{install,activity,rootRelative,batch};
    if(rootIo.inspect(contentRoot).kind!=FolderLinkEntry::Directory||!folders.prepareDirectory(contentRoot,activity)){
        error="The verified update content root is unavailable.";return false;
    }
    std::set<std::string> needsHandling,coveredByFresh;
    // Plan ALL inherited subtrees and their total space before the first move.
    for(const auto& target:candidates){
        if(std::any_of(coveredByFresh.begin(),coveredByFresh.end(),[&](const auto& parent){return inherited_detail::under(target,parent);}))continue;
        const auto relative=target.substr(contentRoot.size()+1);
        CoreContentFolderLinkIo io{install,activity,relative,batch,false};
        const auto current=io.inspect(target);
        bool artifacts=false;
        const FolderLinkPaths paths{target};
        for(const auto& owned:{paths.original(),paths.staged(),paths.journal(),paths.journal()+".unbound-new",paths.journal()+".unbound-old"})
            if(io.inspect(owned).kind!=FolderLinkEntry::Missing)artifacts=true;
        if(current.kind!=FolderLinkEntry::Link&&!artifacts)continue;
        const auto ownership=io.readOwnership(paths,error);
        if(ownership==FolderLinkOwnership::Invalid)return false;
        if(ownership==FolderLinkOwnership::Missing&&artifacts){error="Unowned inherited-content recovery entries were retained: "+target;return false;}
        const bool live=current.kind==FolderLinkEntry::Directory;
        if(!batch.plan(paths,live,error))return false;
        needsHandling.insert(target);
        if(!live)coveredByFresh.insert(target);
    }
    if(!needsHandling.empty()&&!batch.checkSpace(error))return false;
    // All original sources/space are checked before recording a possibly
    // destructive operation. An unreadable disc must remain a paused download.
    if(!needsHandling.empty()&&beforeMutation&&!beforeMutation(error))return false;
    for(const auto& target:candidates){
        const auto relative=target.substr(contentRoot.size()+1);
        CoreContentFolderLinkIo io{install,activity,relative,batch};
        const auto current=io.inspect(target);
        if(needsHandling.count(target)){
            if(!materializeTitleFolderLink(FolderLinkPaths{target},io,error,activity))return false;
            folders.directories.insert(target);
        }else if(current.kind==FolderLinkEntry::Directory)folders.directories.insert(target);
        else if(!folders.prepareDirectory(target,activity)){error=folders.directoryError();return false;}
    }
    return true;
}

bool extractArchiveImpl(const std::string& archive, Mapper mapper, void* context,
                        std::string& error,
                        const ProgressCallback& progress, const InstallCommit& commit,
                        OperationMetrics& metrics, const InstallCommit& beforeWrite,
                        const InstallCommit& beforeMutation, const GameInstall* consoleInstall) {
    if(progress)progress("Opening package","Reading the ZIP directory from the SD card...",-1);
    BufferedZipReader archiveReader;
    if (!archiveReader.open(archive.c_str())) {
        error = "Downloaded file is not a readable ZIP";
        return false;
    }
    zip_t* zip = archiveReader.get();
    metrics.inputBufferBytes = archiveReader.bufferedBytes();
    unsigned long long bytes = 0;
    const zip_int64_t entryCount = zip_get_num_entries(zip, ZIP_FL_UNCHANGED);
    if (entryCount <= 0) { error = "ZIP contains no entries"; return false; }
    if (static_cast<unsigned long long>(entryCount) > config::MaxArchiveFiles) {
        error = "ZIP contains " + std::to_string(entryCount) + " entries; HQ allows at most " +
                std::to_string(config::MaxArchiveFiles); return false;
    }
    std::vector<ArchiveEntry> entries;
    entries.reserve(static_cast<std::size_t>(entryCount));
    unsigned long long mlcRequired = 0, usbRequired = 0, sdRequired = 0;
    const bool testerArchive = archive == config::TesterArchivePath && mapper == coreMapper;
    std::unordered_set<std::string> mappedFiles;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entryCount); ++index) {
        if(progress)progress("Checking package","Checking archive entry "+std::to_string(index+1)+
                             " of "+std::to_string(entryCount)+"...",-1);
        zip_stat_t info{};
        if (zip_stat_index(zip, index, ZIP_FL_UNCHANGED, &info) != 0 || !(info.valid & ZIP_STAT_NAME)) {
            error = "Could not inspect ZIP entry"; return false;
        }
        std::string name(info.name);std::replace(name.begin(), name.end(), '\\', '/');
        const bool directory = !name.empty() && name.back() == '/';if (directory) name.pop_back();
        if(!directory&&!(info.valid&ZIP_STAT_SIZE)){error="ZIP entry is missing its uncompressed size";return false;}
        if (!safeRelativePath(name)) {
            error = "ZIP contains an unsafe archive path: " + name; return false;
        }
        const auto sizeLimit = archiveSizeLimit(bytes, info.size);
        if (sizeLimit == ArchiveSizeLimit::Entry) {
            error = "ZIP file exceeds the 2 GiB per-file limit (" +
                    std::to_string(info.size) + " bytes): " + name; return false;
        }
        if (sizeLimit == ArchiveSizeLimit::Expanded) {
            // Previously accepted entries total at most 5 GiB and this entry is
            // at most 2 GiB, so this diagnostic addition is also bounded.
            error = "ZIP expanded size reaches " + std::to_string(bytes + info.size) +
                    " bytes at " + name + "; HQ allows at most 5 GiB (" +
                    std::to_string(config::MaxArchiveExpandedBytes) + " bytes)";
            return false;
        }
        bytes += info.size;
        if(directory&&(name=="update"||name=="update/code"||name=="update/content"||name=="sd"))continue;
        if (testerArchive && testerReadmeEntry(name, directory, info.size)) continue;
        std::string target,logical;
        if (!mapper(name,target,logical,context)) { error = "Archive contains an unsupported path: " + name; return false; }
        std::string sourceKey=archive,targetKey=target;
        std::transform(sourceKey.begin(),sourceKey.end(),sourceKey.begin(),[](unsigned char c){return char(std::tolower(c));});
        std::transform(targetKey.begin(),targetKey.end(),targetKey.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(sourceKey==targetKey){error="A package cannot replace its own source ZIP. Move the ZIP to another SD folder.";return false;}
        if (reservedInstallPayloadPath(logical)) {
            error = "Archive contains an installer-owned path: " + name +
                    ". Do not package update.txt or temporary replacement files.";
            return false;
        }
        const auto movieRoute=consoleInstall?routeMovieInstallPath(logical,
            consoleInstall->languageFolder):MovieInstallPath{};
        const bool consoleVideo=consoleInstall&&movieRoute.status==MovieInstallPathStatus::Movie;
        if(consoleVideo&&!directory&&movieRoute.destination==movieRoute.movieDirectory){
            error="The package puts a file where a movie folder is required: "+name;return false;
        }
        if(consoleVideo&&!movieInstallTargetFits(target,directory)){
            error="A movie filename or destination is too long to install safely: "+name;return false;
        }
        entries.push_back({index, std::move(name), target, std::move(logical), info.size, directory,consoleVideo});
        if (directory) continue;
        std::string duplicateKey=target;
        if(target.rfind("fs:/vol/external01/",0)==0)std::transform(duplicateKey.begin(),duplicateKey.end(),duplicateKey.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!mappedFiles.insert(duplicateKey).second){error="Archive maps more than one file to: "+target;return false;}
        if (target.rfind("storage_mlc01:",0)==0) mlcRequired += info.size;
        else if (target.rfind("storage_usb01:",0)==0) usbRequired += info.size;
        else if (target.rfind("fs:/vol/external01/",0)==0) sdRequired += info.size;
    }
    if(sdRequired && !hasSpaceFor("fs:/vol/external01",sdRequired,error))return false;
    if (mlcRequired) {
        if(progress)progress("Checking storage","Checking available space on internal storage...",-1);
        if(!hasSpaceFor("storage_mlc01:",mlcRequired,error))return false;
    }
    if (usbRequired) {
        if(progress)progress("Checking storage","Checking available space on USB storage...",-1);
        if(!hasSpaceFor("storage_usb01:",usbRequired,error))return false;
    }
    FileIoContext io;
    if (!io.ready()) { error = "Could not allocate ZIP transfer buffers"; return false; }
    const bool hasMovieFiles=consoleInstall&&std::any_of(entries.begin(),entries.end(),[](const ArchiveEntry& entry){
        return entry.consoleVideo&&!entry.directory;
    });
    if(hasMovieFiles&&!prepareLocalizedMovieDirectories(*consoleInstall,entries,io,error,progress))return false;
    if (consoleInstall &&
        !prepareCoreContentPatchReferences(*consoleInstall, entries, io, error, progress, beforeMutation))
        return false;
    // Verify every destination directory before replacing the first game file.
    // The verified-prefix cache is reused below, so this moves normal directory
    // checks earlier without another payload read or per-file hash pass.
    std::size_t prepared=0;
    for (const auto& entry : entries) {
        if(entry.consoleVideo&&entry.directory&&!hasMovieFiles)continue;
        const auto directory = entry.directory ? entry.target : parentPath(entry.target);
        const std::string count="Destination "+std::to_string(++prepared)+" of "+std::to_string(entries.size());
        const DirectoryActivity activity=[&](const char* operation,const std::string& path){
            if(progress)progress("Preparing destination folders",count+"\n"+operation+": "+path,-1);
        };
        if (!io.prepareDirectory(directory,activity)) { error = io.directoryError(); return false; }
    }
    // A local diagnostic must invalidate its published-version claim only after
    // the same complete preflight as a normal install, but before any payload
    // is replaced. Normal installs supply no hook and do no additional I/O.
    if (beforeWrite) {
        if (mappedFiles.empty()) {
            error = "The test ZIP contains directories but no payload files";
            return false;
        }
        if(progress)progress("Preparing test installation","Saving the local test version marker before replacing files...",-1);
        if (!beforeWrite(error)) return false;
    }
    if(beforeMutation&&!beforeMutation(error))return false;
    InstallProgress installProgress{&progress,bytes,0,{}};
    installProgress.report("Preparing package files...");
    // Preflight validated every path and destination on this open archive.
    // Reuse that plan rather than remapping and restating each entry while writing.
    for (const auto& entry : entries) {
        if (entry.directory) {
            if(entry.consoleVideo&&!hasMovieFiles)continue;
            if (!io.prepareDirectory(entry.target)) { error = io.directoryError(); return false; }
        } else if (!extractCurrent(zip, entry.index, entry.name, entry.target,
                                   installProgress, io, entry.bytes, metrics, error)) {
            return false;
        }
    }
    installProgress.finishing("Closing the package and saving installation records...");
    if (!archiveReader.close()) { error = "Could not close ZIP cleanly"; return false; }
    if (commit && !commit(error)) return false;
    installProgress.finish();
    return true;
}

bool extractArchive(const std::string& archive, Mapper mapper, void* context,
                    std::string& error,
                    const ProgressCallback& progress, const InstallCommit& commit = {},
                    const InstallCommit& beforeWrite = {}, const InstallCommit& beforeMutation = {},
                    const GameInstall* consoleInstall = nullptr) {
    if(mapper==coreMapper)consoleInstall=static_cast<const GameInstall*>(context);
    const auto* timingInstall=consoleInstall;
    struct stat timingArchive{};const auto compressedBytes=lstat(archive.c_str(),&timingArchive)==0&&timingArchive.st_size>0?
        static_cast<unsigned long long>(timingArchive.st_size):0;
    // Ghosts uses its native update layout, without BO2 folder-link migration.
    if(consoleInstall&&consoleInstall->sku&&consoleInstall->sku->game==Game::Ghosts)consoleInstall=nullptr;
    OperationMetrics metrics;
    bool usedWorker = false;
    const auto started = operationMetricsNow();
    InstallActivity activity(SDL_GetTicks());
    const ProgressCallback display=progress?ProgressCallback{[&](const std::string& title,const std::string& detail,int percent){
        const auto now=SDL_GetTicks();
        activity.observe(title,detail,percent,now);
        const auto frame=activity.snapshot(now);
        progress(frame.title,frame.detail,frame.percent);
    }}:ProgressCallback{};
    const BackgroundTickCallback pulse=progress?BackgroundTickCallback{[&]{
        if(operationFrameCallback)operationFrameCallback();
        const auto now=SDL_GetTicks();
        if(activity.pulseDue(now)){
            const auto frame=activity.snapshot(now);
            progress(frame.title,frame.detail,frame.percent);
        }
    }}:BackgroundTickCallback{};
    const bool ok = runBackgroundOperation("HQ install", display,
        [&](const ProgressCallback& workerProgress) {
            return extractArchiveImpl(archive, mapper, context, error,
                                      workerProgress, commit, metrics, beforeWrite, beforeMutation, consoleInstall);
        }, &usedWorker,pulse);
    // Joining also completes per-file replacement and final marker commit before the caller can
    // inspect error state or clean up the downloaded archive.
    metrics.totalTicks = operationMetricsNow() - started;
    const std::string fields =
        "total_ms=" + std::to_string(operationMetricsMilliseconds(metrics.totalTicks)) +
        "|compressed_bytes="+std::to_string(compressedBytes)+
        "|device="+(timingInstall?std::string(mapStorageTag(timingInstall->update.device)):std::string("SD"))+
        "|zip_ms=" + std::to_string(operationMetricsMilliseconds(metrics.archiveReadTicks)) +
        "|write_ms=" + std::to_string(operationMetricsMilliseconds(metrics.writeTicks)) +
        "|close_ms=" + std::to_string(operationMetricsMilliseconds(metrics.closeTicks)) +
        "|bytes=" + std::to_string(metrics.bytesWritten) +
        "|zip_bytes=" + std::to_string(metrics.bytesRead) +
        "|queue_wait_ms=" + std::to_string(operationMetricsMilliseconds(metrics.queueWaitTicks)) +
        "|pipelined_files=" + std::to_string(metrics.pipelinedFiles) +
        "|sync_files=" + std::to_string(metrics.synchronousFiles) +
        "|input_buffer_bytes=" + std::to_string(metrics.inputBufferBytes) +
        "|mode=" + (usedWorker ? "worker" : "synchronous");
    appendPerformanceLog("install", ok, fields);
    return ok;
}

bool coreMapper(const std::string& name, std::string& target, std::string& logical, void* opaque) {
    const auto& install=*static_cast<const GameInstall*>(opaque);
    const auto route=packageRoute(name,install.languageFolder);
    target=packageDestination(route,install,config::activeGame);logical=route.logical;
    return !target.empty();
}

struct WorkshopContext { const GameInstall* install; const WorkshopMod* mod; };
bool workshopMapper(const std::string& name, std::string& target, std::string& logical, void* opaque) {
    auto& context = *static_cast<WorkshopContext*>(opaque);
    if (!context.install->update.present || !context.install->destinationReady ||
        !safeRelativePath(name)) return false;
    logical = "workshop/" + context.mod->id + "/" + name;
    target = context.install->update.root + "/content/mods/" + context.mod->id + "/" + name;
    return true;
}

std::string lowerValue(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string pluginReceiptPath(const WorkshopPlugin& plugin) {
    return std::string(config::PluginStateRoot) + "/" + lowerValue(plugin.key) + ".txt";
}

std::string pluginTransactionPath(const WorkshopPlugin& plugin) {
    return pluginReceiptPath(plugin) + ".transaction";
}

std::string pluginPreinstallReceiptPath(const WorkshopPlugin& plugin) {
    return pluginReceiptPath(plugin) + ".preinstall";
}

std::string pluginTargetPath(const std::string& archivePath) {
    static const std::string Prefix = "wiiu/environments/aroma/";
    if (archivePath.rfind(Prefix, 0) != 0 || archivePath.size() <= Prefix.size()) return {};
    return std::string(config::AromaRoot) + "/" + archivePath.substr(Prefix.size());
}

std::string serializePluginRelease(const PluginRelease& release,
                                   const char* heading = "UNBOUND_PLUGIN_RECEIPT_V1") {
    std::string output = std::string("# ") + heading + "\n";
    output += release.pluginKey + "|" + std::to_string(release.revision) + "|" +
              release.version + "|" + release.downloadUrl + "|" + release.sha256 + "|" +
              std::to_string(release.compressedBytes) + "\n";
    for (const auto& file : release.files)
        output += "FILE|" + file.archivePath + "|" + file.sha256 + "|" +
                  std::to_string(file.bytes) + "\n";
    return output;
}

bool loadPluginReceipt(const WorkshopPlugin& plugin, PluginRelease& receipt, bool& found,
                       std::string& error, bool allowRecoveryWrites = true) {
    found = false;
    receipt = {};
    const std::string path = pluginReceiptPath(plugin);
    if (allowRecoveryWrites) {
        if (!recoverAtomicTextFile(path)) {
            error = "Could not recover the installed plugin receipt";
            return false;
        }
    } else if (exists(path + ".unbound-new") || exists(path + ".unbound-old")) {
        error = "The installed plugin receipt needs recovery; HQ is read-only this session";
        return false;
    }
    if (!exists(path)) return true;
    std::string text;
    if (!readTextFile(path, text, config::MaxManifestBytes)) {
        error = "Could not read the installed plugin receipt";
        return false;
    }
    std::vector<std::string> parseErrors;
    if (!parsePluginRelease(text, plugin, receipt, &parseErrors)) {
        error = parseErrors.empty() ? "The installed plugin receipt is malformed"
                                    : "Installed plugin receipt: " + parseErrors.front();
        return false;
    }
    found = true;
    return true;
}

bool pluginFilesMatch(const PluginRelease& release, std::string& error) {
    for (const auto& file : release.files) {
        const std::string target = pluginTargetPath(file.archivePath);
        struct stat info{};
        if (target.empty() || stat(target.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
            static_cast<std::uint64_t>(info.st_size) != file.bytes) {
            error = "Installed file is missing or has the wrong size: " + file.archivePath;
            return false;
        }
        std::string actual;
        if (!sha256File(target, actual) || lowerValue(actual) != lowerValue(file.sha256)) {
            error = "Installed file failed its integrity check: " + file.archivePath;
            return false;
        }
    }
    return true;
}

bool curatedTargetsPresent(const WorkshopPlugin& plugin, bool& any, bool& all,
                           std::string& error) {
    any = false;
    all = true;
    for (const auto& archivePath : plugin.managedPaths) {
        const std::string target = pluginTargetPath(archivePath);
        if (target.empty()) {
            error = "The curated plugin path is invalid: " + archivePath;
            return false;
        }
        struct stat info{};
        if (stat(target.c_str(), &info) == 0) {
            if (!S_ISREG(info.st_mode)) {
                error = "A plugin destination is not a regular file: " + archivePath;
                return false;
            }
            any = true;
        } else {
            all = false;
        }
    }
    return true;
}

bool parsePluginJournal(const WorkshopPlugin& plugin, const std::string& text,
                        std::uint64_t& revision,
                        std::unordered_map<std::string, bool>& existed,
                        std::string& error) {
    revision = 0;
    existed.clear();
    std::unordered_set<std::string> approved;
    for (const auto& path : plugin.managedPaths) approved.insert(lowerValue(path));
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto fields = splitEscaped(line, '|');
        if (fields.size() == 2 && fields[0] == "REVISION") {
            if (revision || fields[1].empty() || !std::all_of(fields[1].begin(), fields[1].end(),
                [](unsigned char value) { return std::isdigit(value); })) {
                error = "The plugin recovery journal has an invalid revision";
                return false;
            }
            for (const unsigned char value : fields[1]) {
                const auto digit = static_cast<std::uint64_t>(value - '0');
                if (revision > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                    error = "The plugin recovery journal revision is too large";
                    return false;
                }
                revision = revision * 10 + digit;
            }
            continue;
        }
        if (fields.size() != 3 || fields[0] != "FILE" ||
            (fields[2] != "0" && fields[2] != "1") ||
            approved.find(lowerValue(fields[1])) == approved.end() ||
            !existed.emplace(lowerValue(fields[1]), fields[2] == "1").second) {
            error = "The plugin recovery journal contains an invalid file record";
            return false;
        }
    }
    if (!revision || existed.size() != approved.size()) {
        error = "The plugin recovery journal is incomplete";
        return false;
    }
    return true;
}

bool cleanupPluginArtifacts(const WorkshopPlugin& plugin) {
    bool ok = true;
    for (const auto& path : plugin.managedPaths) {
        const std::string target = pluginTargetPath(path);
        if (exists(target + ".unbound-new") && std::remove((target + ".unbound-new").c_str()) != 0)
            ok = false;
        if (exists(target + ".unbound-old") && std::remove((target + ".unbound-old").c_str()) != 0)
            ok = false;
    }
    const std::string preinstall = pluginPreinstallReceiptPath(plugin);
    if (exists(preinstall) && std::remove(preinstall.c_str()) != 0) ok = false;
    return ok;
}

bool recoverPluginTransaction(const WorkshopPlugin& plugin, std::string& error) {
    const std::string journal = pluginTransactionPath(plugin);
    const std::string preinstall = pluginPreinstallReceiptPath(plugin);
    if (!recoverAtomicTextFile(journal)) {
        error = "Could not recover the plugin transaction journal";
        return false;
    }
    if (!recoverAtomicTextFile(preinstall)) {
        error = "Could not recover the plugin receipt backup";
        return false;
    }
    if (!exists(journal)) {
        if (exists(preinstall) && std::remove(preinstall.c_str()) != 0) {
            error = "Could not clear an unused plugin receipt backup";
            return false;
        }
        for (const auto& path : plugin.managedPaths) {
            const std::string target = pluginTargetPath(path);
            if (exists(target + ".unbound-new") || exists(target + ".unbound-old")) {
                error = "Unowned plugin recovery files were found beside " + path +
                        ". HQ left them untouched; remove or restore them on a PC before retrying.";
                return false;
            }
        }
        return true;
    }
    std::string text;
    if (!readTextFile(journal, text, config::MaxManifestBytes)) {
        error = "Could not read the plugin transaction journal";
        return false;
    }
    std::uint64_t revision = 0;
    std::unordered_map<std::string, bool> existedBefore;
    if (!parsePluginJournal(plugin, text, revision, existedBefore, error)) return false;

    PluginRelease currentReceipt;
    bool receiptFound = false;
    std::string receiptError;
    const bool receiptReadable = loadPluginReceipt(plugin, currentReceipt, receiptFound,
                                                   receiptError);
    std::string matchError;
    const bool committed = receiptReadable && receiptFound &&
                           currentReceipt.revision == revision &&
                           pluginFilesMatch(currentReceipt, matchError);
    if (committed) {
        if (!cleanupPluginArtifacts(plugin) || std::remove(journal.c_str()) != 0) {
            error = "The plugin update finished, but its recovery files could not be cleaned";
            return false;
        }
        return true;
    }

    // The receipt was not committed, so the journal proves these exact
    // suffix files belong to HQ's interrupted transaction. Restore the prior
    // installation without touching any other SD paths.
    bool anyPreviousRenamed = false;
    std::size_t highestRenamedIndex = 0;
    for (std::size_t index = 0; index < plugin.managedPaths.size(); ++index) {
        const std::string target = pluginTargetPath(plugin.managedPaths[index]);
        if (exists(target + ".unbound-old")) {
            anyPreviousRenamed = true;
            highestRenamedIndex = index;
        }
    }
    for (std::size_t index = 0; index < plugin.managedPaths.size(); ++index) {
        const auto& path = plugin.managedPaths[index];
        const std::string target = pluginTargetPath(path);
        const bool hadOriginal = existedBefore[lowerValue(path)];
        const std::string previous = target + ".unbound-old";
        if (hadOriginal) {
            if (exists(previous)) {
                if (exists(target) && std::remove(target.c_str()) != 0) {
                    error = "Could not remove an interrupted plugin file while recovering " + path;
                    return false;
                }
                if (std::rename(previous.c_str(), target.c_str()) != 0) {
                    error = "Could not restore the previous plugin file " + path;
                    return false;
                }
            } else {
                // Original files are renamed in catalog order. With no prior
                // .unbound-old, or only earlier entries renamed, this target
                // has not been touched yet and must be left in place.
                const bool untouched = (!anyPreviousRenamed || index > highestRenamedIndex) &&
                                       exists(target);
                if (!untouched) {
                    error = "The interrupted plugin update is missing its recovery copy for " + path;
                    return false;
                }
            }
        } else if (exists(target) && std::remove(target.c_str()) != 0) {
            error = "Could not remove an interrupted plugin file while recovering " + path;
            return false;
        }
        const std::string temporary = target + ".unbound-new";
        if (exists(temporary) && std::remove(temporary.c_str()) != 0) {
            error = "Could not remove an interrupted staged plugin file";
            return false;
        }
    }
    const std::string receiptPath = pluginReceiptPath(plugin);
    if (exists(preinstall)) {
        if (exists(receiptPath) && std::remove(receiptPath.c_str()) != 0) {
            error = "Could not replace the interrupted plugin receipt";
            return false;
        }
        if (std::rename(preinstall.c_str(), receiptPath.c_str()) != 0) {
            error = "Could not restore the previous plugin receipt";
            return false;
        }
    } else if (exists(receiptPath) && std::remove(receiptPath.c_str()) != 0) {
        error = "Could not remove the interrupted plugin receipt";
        return false;
    }
    if (std::remove(journal.c_str()) != 0) {
        error = "The plugin files were restored, but the recovery journal remains";
        return false;
    }
    return true;
}

bool validatePluginZip(const std::string& archive, const WorkshopPlugin& plugin,
                       const PluginRelease& release, unsigned long long& expandedBytes,
                       std::string& error) {
    expandedBytes = 0;
    if (release.pluginKey != plugin.key || release.files.size() != plugin.managedPaths.size() ||
        release.files.empty() || release.files.size() > config::MaxPluginFiles) {
        error = "The plugin release does not match its curated catalog entry";
        return false;
    }
    AlignedIoBuffer buffer;
    if (!buffer) { error = "Could not allocate plugin ZIP verification buffer"; return false; }
    std::unordered_map<std::string, const PluginFile*> expected;
    std::unordered_set<std::string> allowedDirectories;
    for (const auto& file : release.files) {
        expected.emplace(lowerValue(file.archivePath), &file);
        std::size_t slash = 0;
        while ((slash = file.archivePath.find('/', slash)) != std::string::npos) {
            allowedDirectories.insert(lowerValue(file.archivePath.substr(0, slash)));
            ++slash;
        }
    }
    int zipError = 0;
    zip_t* zip = zip_open(archive.c_str(), ZIP_RDONLY, &zipError);
    if (!zip) { error = "Downloaded plugin package is not a readable ZIP"; return false; }
    std::unordered_set<std::string> seen;
    const zip_int64_t count = zip_get_num_entries(zip, ZIP_FL_UNCHANGED);
    if (count <= 0 || static_cast<unsigned long long>(count) > config::MaxPluginFiles + 16) {
        error = "Plugin ZIP contains an invalid number of entries";
        zip_close(zip);
        return false;
    }
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
        zip_stat_t info{};
        if (zip_stat_index(zip, index, ZIP_FL_UNCHANGED, &info) != 0 ||
            !(info.valid & ZIP_STAT_NAME)) {
            error = "Could not inspect a plugin ZIP entry";
            zip_close(zip);
            return false;
        }
        std::string name(info.name);
        if (name.find('\\') != std::string::npos) {
            error = "Plugin ZIP uses unsupported backslash paths";
            zip_close(zip);
            return false;
        }
        const bool directory = !name.empty() && name.back() == '/';
        if (directory) name.pop_back();
        const std::string key = lowerValue(name);
        if (!safeRelativePath(name)) {
            error = "Plugin ZIP contains an unsafe path: " + name;
            zip_close(zip);
            return false;
        }
        if (directory) {
            if (allowedDirectories.find(key) == allowedDirectories.end()) {
                error = "Plugin ZIP contains an undeclared directory: " + name;
                zip_close(zip);
                return false;
            }
            continue;
        }
        const auto expectedFile = expected.find(key);
        if (expectedFile == expected.end() || expectedFile->second->archivePath != name ||
            !seen.insert(key).second || !(info.valid & ZIP_STAT_SIZE) ||
            info.size != expectedFile->second->bytes ||
            expandedBytes > config::MaxPluginExpandedBytes ||
            info.size > config::MaxPluginExpandedBytes - expandedBytes) {
            error = "Plugin ZIP contains an undeclared, duplicate, or incorrectly sized file: " + name;
            zip_close(zip);
            return false;
        }
        expandedBytes += info.size;
        zip_file_t* entry = zip_fopen_index(zip, index, ZIP_FL_UNCHANGED);
        if (!entry) { error = "Could not read plugin ZIP entry: " + name; zip_close(zip); return false; }
        Sha256 hash;
        bool ok = true;
        for (;;) {
            const zip_int64_t bytes = zip_fread(entry, buffer.data(), buffer.size());
            if (bytes < 0) { ok = false; break; }
            if (!bytes) break;
            hash.update(buffer.data(), static_cast<std::size_t>(bytes));
        }
        if (zip_fclose(entry) != 0) ok = false;
        if (!ok || lowerValue(hash.finish()) != lowerValue(expectedFile->second->sha256)) {
            error = "Plugin ZIP file failed its SHA-256 check: " + name;
            zip_close(zip);
            return false;
        }
    }
    if (seen.size() != expected.size()) {
        error = "Plugin ZIP is missing one or more declared files";
        zip_close(zip);
        return false;
    }
    if (zip_close(zip) != 0) { error = "Could not close the plugin ZIP cleanly"; return false; }
    return true;
}
}

bool runInheritedRepairTask(const GameInstall& install,InheritedContentRepairInfo& information,
                            std::string& error,const ProgressCallback& progress,bool apply){
    const auto began=operationMetricsNow();bool usedWorker=false;
    InstallActivity activity(SDL_GetTicks());
    const ProgressCallback display=progress?ProgressCallback{[&](const std::string& title,const std::string& detail,int percent){
        const auto now=SDL_GetTicks();activity.observe(title,detail,percent,now);
        const auto view=activity.snapshot(now);progress(view.title,view.detail,view.percent);
    }}:ProgressCallback{};
    const BackgroundTickCallback tick=progress?BackgroundTickCallback{[&]{
        if(operationFrameCallback)operationFrameCallback();
        const auto now=SDL_GetTicks();if(activity.pulseDue(now)){
            const auto view=activity.snapshot(now);progress(view.title,view.detail,view.percent);
        }
    }}:BackgroundTickCallback{};
    const bool result=runBackgroundOperation(apply?"HQ inherited content repair":"HQ inherited content scan",display,
        [&](const ProgressCallback& relay){return inheritedRepairOperation(install,information,error,relay,apply);},&usedWorker,tick);
    if(apply)appendPerformanceLog("preserve",result,
        "total_ms="+std::to_string(operationMetricsMilliseconds(operationMetricsNow()-began))+
        "|planned_bytes="+std::to_string(information.copyBytes)+
        "|required_bytes="+std::to_string(information.requiredBytes)+
        "|mode="+(usedWorker?"worker":"synchronous"));
    return result;
}
bool inspectInheritedContentRepair(const GameInstall& install,InheritedContentRepairInfo& information,
                                    std::string& error,const ProgressCallback& progress){
    return runInheritedRepairTask(install,information,error,progress,false);
}
bool repairInheritedContent(const GameInstall& install,std::string& error,const ProgressCallback& progress){
    InheritedContentRepairInfo information;
    return runInheritedRepairTask(install,information,error,progress,true);
}

// READ-ONLY evidence for FSMakeLink calibration (see native_link.hpp): records
// the complete raw FSA stat of installer-made link entries beside their BASE
// counterparts, so the stored link metadata can be compared with the request
// fields. No FSA write, mount or link command is issued.
bool writeNativeLinkCensus(const GameInstall& install,std::string& error){
    error.clear();
    if(!install.sku||!install.update.present||!validatedInheritedTarget(install,error)){
        if(error.empty())error="No verified installed update.";
        return false;
    }
    GameInstall physical=install;findPhysicalBase(physical,&install);
    const auto raw=[](const std::string& path)->std::string{
        for(const char* device:{"storage_usb01","storage_mlc01"}){
            const auto prefix=std::string(device)+":/";
            if(path.rfind(prefix,0)==0)return "/vol/"+std::string(device)+"/"+path.substr(prefix.size());
        }
        return {};
    };
    const auto updateRoot=raw(install.update.root+"/content");
    const auto baseRoot=physical.base.present&&(physical.base.device==Device::MLC||physical.base.device==Device::USB)
        ?raw(stockTitleRoot(physical.base.device,install.sku->baseTitleId)+"/content"):std::string{};
    std::string report="HQ "+std::string(config::AppVersion)+" native link census (read-only)\n"
        "FSStat layout: flags mode owner group size allocSize quotaSize(64) entryId created(64) modified(64) unknown[0x30]\n"
        "UPDATE: "+updateRoot+"\nBASE: "+(baseRoot.empty()?std::string("unavailable (disc or not found)"):baseRoot)+"\n";
    constexpr std::size_t MaximumReport=256*1024;
    const auto hex=[](const FSAStat& stat){
        const auto* bytes=reinterpret_cast<const unsigned char*>(&stat);
        static const char digits[]="0123456789abcdef";std::string text;
        for(std::size_t i=0;i<sizeof(FSAStat);++i){if(i&&i%4==0)text+=' ';text+=digits[bytes[i]>>4];text+=digits[bytes[i]&15];}
        return text;
    };
    if(updateRoot.empty()){error="The update path is not a console storage path.";return false;}
    if(FSAInit()!=FS_ERROR_OK){error="FSAInit failed.";return false;}
    const auto client=FSAAddClient(nullptr);
    if(client<0){error="FSAAddClient failed.";return false;}
    if(Mocha_UnlockFSClientEx(client)!=MOCHA_RESULT_SUCCESS){FSADelClient(client);error="Mocha could not unlock the census client.";return false;}
    std::vector<std::pair<std::string,unsigned>> pending{{{},0}};
    std::size_t visited=0,links=0;
    for(std::size_t position=0;position<pending.size()&&report.size()<MaximumReport;++position){
        const auto [relative,depth]=pending[position];
        const auto directory=updateRoot+(relative.empty()?"":"/"+relative);
        FSADirectoryHandle handle{};
        const auto opened=FSAOpenDir(client,directory.c_str(),&handle);
        if(opened!=FS_ERROR_OK){report+="opendir "+directory+" FSA="+std::to_string(static_cast<int>(opened))+"\n";continue;}
        alignas(64) FSADirectoryEntry entry{};
        for(;;){
            const auto read=FSAReadDir(client,handle,&entry);
            if(read==FS_ERROR_END_OF_DIR)break;
            if(read!=FS_ERROR_OK){report+="readdir "+directory+" FSA="+std::to_string(static_cast<int>(read))+"\n";break;}
            if(++visited>8192)break;
            const std::string name(entry.name,strnlen(entry.name,sizeof(entry.name)));
            const auto child=relative.empty()?name:relative+"/"+name;
            const auto flags=static_cast<std::uint32_t>(entry.info.flags);
            if(flags&FS_STAT_LINK){
                if(++links>384)continue;
                report+="LINK "+child+"\n  readdir "+hex(entry.info)+"\n";
                FSAStat direct{};
                const auto stated=FSAGetStat(client,(updateRoot+"/"+child).c_str(),&direct);
                report+="  stat FSA="+std::to_string(static_cast<int>(stated))+(stated==FS_ERROR_OK?" "+hex(direct):"")+"\n";
                // Retained HQ references point at the name they were created with.
                auto baseName=child;const auto retained=baseName.find(".unbound-link-original");
                if(retained!=std::string::npos)baseName.erase(retained);
                if(!baseRoot.empty()){
                    FSAStat base{};
                    const auto baseStat=FSAGetStat(client,(baseRoot+"/"+baseName).c_str(),&base);
                    report+="  base "+baseName+" FSA="+std::to_string(static_cast<int>(baseStat))+(baseStat==FS_ERROR_OK?" "+hex(base):"")+"\n";
                }
            }else if((flags&FS_STAT_DIRECTORY)&&depth<20&&pending.size()<4096)pending.push_back({child,depth+1});
        }
        FSACloseDir(client,handle);
    }
    FSADelClient(client);
    report+="Visited: "+std::to_string(visited)+"; link entries: "+std::to_string(links)+"\n";
    return writeTextFileAtomic(std::string(config::SdRoot)+"/native-link-census.txt",report.substr(0,MaximumReport),error);
}

#include "support_export_platform.inc"

bool ensureDirectory(const std::string& path) {
    PosixDirectoryIo io;
    std::unordered_set<std::string> directories;
    DirectoryFailure failure;
    const bool ok = prepareDirectoryPrefixes(path, directoryPrefixes(path), directories, io, failure);
    if (!ok) errno = failure.error;
    return ok;
}

bool initializePlatform(std::string& error, const ProgressCallback& progress) {
    if (progress) progress("Starting Unbound HQ", "Checking Aroma's existing SD-card volume...", 5);
    struct stat sdInfo{};
    if (stat("fs:/vol/external01", &sdInfo) != 0 || !S_ISDIR(sdInfo.st_mode)) {
        error = "Aroma did not expose the SD card at fs:/vol/external01";
        return false;
    }
    if constexpr (!config::DiagnosticSdBuild) {
        // The runtime notifier was removed. Clear only its former HQ-owned
        // marker for the regular app. Local diagnostics do not touch it.
        std::remove(config::LegacyUpdateAvailableFlagPath);
        std::remove((std::string(config::LegacyUpdateAvailableFlagPath) + ".unbound-new").c_str());
        std::remove((std::string(config::LegacyUpdateAvailableFlagPath) + ".unbound-old").c_str());
        // WUT initializes the socket subsystem and calls ACConnectAsync before
        // main(). Do not initialize it a second time. The async connection can,
        // however, still be in progress when the first GitHub request is made.
        if (progress) progress("Starting Unbound HQ", "Waiting for the Wii U network connection...", 8);
        BOOL connected = FALSE;
        constexpr int NetworkPollCount = 80; // 20 seconds at 250 ms per poll.
        for (int attempt = 0; attempt < NetworkPollCount; ++attempt) {
            if (NNResult_IsSuccess(ACIsApplicationConnected(&connected)) && connected) break;
            if (progress && attempt % 4 == 0) {
                progress("Starting Unbound HQ",
                         "Waiting for network... " + std::to_string(attempt / 4 + 1) + "s",
                         8 + (attempt * 4) / NetworkPollCount);
            }
            OSSleepTicks(OSMillisecondsToTicks(250));
        }
        if (progress) {
            progress("Starting Unbound HQ",
                     connected ? "Network connected; starting HTTPS..."
                               : "Network is offline; HQ will use cached information.",
                     12);
        }
        if (progress) progress("Starting Unbound HQ", "Initializing the download service...", 14);
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { error = "libcurl initialization failed"; return false; }
        curlReady = true;
    } else if (progress) {
        progress("Starting diagnostic HQ", "Local SD testing; online update checks are disabled.", 14);
    }
    if (progress) progress("Starting Unbound HQ", "Connecting to the Aroma/Mocha storage API...", 20);
    if (Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS) { error = "Aroma/Mocha is required"; return false; }
    mochaReady = true;
    if (progress) progress("Starting Unbound HQ", "Preparing the download cache on SD...", 28);
    if (!ensureDirectory(config::gameCacheRoot())) {
        error = std::string("Could not create the SD cache directory: ") + std::strerror(errno);
        return false;
    }
    if (progress) progress("Starting Unbound HQ", "SD and Aroma/Mocha are ready.", 32);
    return true;
}

void setOperationFrameCallback(std::function<void()> callback) {
    operationFrameCallback=std::move(callback);
}
void shutdownPlatform() {
    operationFrameCallback={};
    for(unsigned volume=0;volume<DiscSourceVolumeCount;++volume){
        if(discSourceMounted[volume])Mocha_UnmountFS(DiscSourceVolumes[volume].alias);
        discSourceMounted[volume]=false;
    }
    if (usbMounted) Mocha_UnmountFS("storage_usb01");
    if (mlcMounted) Mocha_UnmountFS("storage_mlc01");
    if (mochaReady) Mocha_DeInitLibrary();
    if (curlReady) curl_global_cleanup();
    usbMounted = mlcMounted = mochaReady = curlReady = false;
}

bool detectInstallation(GameInstall& install, std::string& error,
                        const ProgressCallback& progress, bool allowRecoveryWrites) {
    install.game = config::activeGame;
    const std::string finding = std::string("Finding ") + gameName(install.game);
    if (progress) progress(finding, "Opening the Wii U title database...", 36);
    const int handle = MCP_Open();
    if (handle < 0) { error = "Could not open MCP"; return false; }
    MCPSysProdSettings settings{};
    if (progress) progress(finding, "Reading the console region...", 40);
    if (MCP_GetSysProdSettings(handle, &settings) >= 0) {
        if (settings.game_region & MCP_REGION_USA) install.consoleRegion = Region::USA;
        else if (settings.game_region & MCP_REGION_EUROPE) install.consoleRegion = Region::Europe;
        else if (settings.game_region & MCP_REGION_JAPAN) install.consoleRegion = Region::Japan;
    }
    // Ask MCP only for the known update IDs. The bulk title-list IOS request
    // requires aligned transfer memory and has proven unreliable on hardware.
    alignas(64) MCPTitleListType selectedTitle{};
    alignas(64) MCPTitleListType fallbackTitle{};
    const GameSku* fallbackSku = nullptr;
    unsigned skuIndex = 0;
    for (const auto& sku : gameSkus(config::activeGame)) {
        if (progress) progress(finding, std::string("Checking ") + sku.label + " update...", 44 + static_cast<int>(skuIndex++) * 4);
        alignas(64) MCPTitleListType title{};
        bool found = MCP_GetTitleInfo(handle, sku.updateTitleId, &title) >= 0;
        if (!found) found = MCP_GetTitleInfo(handle, sku.baseTitleId, &title) >= 0;
        if (!found) continue;
        if (!fallbackSku) {
            fallbackSku = &sku;
            fallbackTitle = title;
        }
        if (sku.region == install.consoleRegion) {
            install.sku = &sku;
            selectedTitle = title;
            break;
        }
    }
    // Inserted disc titles are reported by the ODD device list rather than as
    // installed MLC/USB applications on some system versions.
    if (!install.sku) {
        alignas(64) MCPTitleListType discTitles[16]{};
        std::uint32_t discCount = 16;
        if (MCP_TitleListByDevice(handle, "odd", &discCount, discTitles, sizeof(discTitles)) >= 0) {
            for (std::uint32_t i = 0; i < std::min<std::uint32_t>(discCount, 16); ++i) {
                const auto* sku = findSkuByTitle(discTitles[i].titleId);
                if (!sku || sku->game != install.game || discTitles[i].titleId != sku->baseTitleId) continue;
                if (!fallbackSku) { fallbackSku = sku; fallbackTitle = discTitles[i]; }
                if (sku->region == install.consoleRegion) {
                    install.sku = sku;
                    selectedTitle = discTitles[i];
                    break;
                }
            }
        }
    }
    if (!install.sku && fallbackSku) {
        install.sku = fallbackSku;
        selectedTitle = fallbackTitle;
    }
    if (!install.sku) {
        MCP_Close(handle);
        install.modVersion = "Not installed";
        error = std::string("No installed ") + gameName(install.game) + " game or inserted supported disc was found";
        if (progress) progress(finding, "No game install or disc was found; continuing in browse-only mode.", 100);
        return true;
    }

    std::string locale = "en";
    if (SDL_Locale* locales = SDL_GetPreferredLocales()) {
        if (locales[0].language) locale = locales[0].language;
        SDL_free(locales);
    }
    install.languageFolder = languageFolderForSku(install.sku, locale);

    alignas(64) MCPTitleListType baseTitle{};
    if (MCP_GetTitleInfo(handle, install.sku->baseTitleId, &baseTitle) >= 0) {
        install.base.device = deviceFromTitle(baseTitle);
        install.base.present = install.base.device != Device::Missing;
        install.base.titleVersion = baseTitle.titleVersion;
    } else if (selectedTitle.titleId == install.sku->baseTitleId) {
        install.base.device = deviceFromTitle(selectedTitle);
        install.base.present = install.base.device != Device::Missing;
        install.base.titleVersion = selectedTitle.titleVersion;
    }
    if (!install.base.present) {
        alignas(64) MCPTitleListType discTitles[16]{};
        std::uint32_t discCount = 16;
        if (MCP_TitleListByDevice(handle, "odd", &discCount, discTitles, sizeof(discTitles)) >= 0) {
            for (std::uint32_t i = 0; i < std::min<std::uint32_t>(discCount, 16); ++i) {
                if (discTitles[i].titleId != install.sku->baseTitleId) continue;
                install.base.device = Device::Disc;
                install.base.present = true;
                install.base.titleVersion = discTitles[i].titleVersion;
                break;
            }
        }
    }
    if(install.base.present&&(install.base.device==Device::MLC||install.base.device==Device::USB))
        install.base.root=titleRoot(install.base.device,install.sku->baseTitleId);

    alignas(64) MCPTitleListType updateTitle{};
    if (MCP_GetTitleInfo(handle, install.sku->updateTitleId, &updateTitle) >= 0) {
        install.update.device = deviceFromTitle(updateTitle);
        install.update.present = install.update.device != Device::Missing;
        install.update.titleVersion = updateTitle.titleVersion;
        if (install.update.present)
            install.update.root = titleRoot(install.update.device, install.sku->updateTitleId);
    }

    alignas(64) MCPTitleListType aocTitle{};
    if (progress) progress(finding, "Checking the matching DLC/AOC installation...", 66);
    if (MCP_GetTitleInfo(handle, install.sku->aocTitleId, &aocTitle) >= 0) {
        install.aoc.device = deviceFromTitle(aocTitle);
        install.aoc.present = install.aoc.device != Device::Missing;
        install.aoc.titleVersion = aocTitle.titleVersion;
        if (install.aoc.present)
            install.aoc.root = titleRoot(install.aoc.device, install.sku->aocTitleId);
    }
    MCP_Close(handle);
    if (install.update.present) {
        if (progress) progress(finding, std::string("Mounting game update from ") + deviceName(install.update.device) + "...", 74);
        if (!mountDevice(install.update.device)) {
            error = "The game update device could not be mounted";
            install.update = {};
        }
    }
    if (progress) progress(finding, "Reading the installed Unbound version marker...", 93);
    if (install.update.present) {
        const std::string versionFile = install.update.root + "/" + config::VersionRelativePath;
        const std::string previousVersionFile = versionFile + ".unbound-old";
        bool readVersion = readTextFile(versionFile, install.modVersion, 65536);
        if (!readVersion && exists(previousVersionFile)) {
            if (allowRecoveryWrites && std::rename(previousVersionFile.c_str(), versionFile.c_str()) == 0)
                readVersion = readTextFile(versionFile, install.modVersion, 65536);
            else
                readVersion = readTextFile(previousVersionFile, install.modVersion, 65536);
        }
        if (!readVersion) install.modVersion = "Not installed";
        else install.modVersion = coreMarkerVersion(install.modVersion);
    } else {
        install.modVersion = "Not installed";
    }
    if (progress) progress(finding, "Storage detection completed.", 100);
    return true;
}

GameLaunchTarget prepareGameLaunch(const GameInstall& install) {
    const int handle = MCP_Open();
    alignas(64) MCPTitleListType discs[16]{};
    std::uint32_t discCount = 16;
    if (handle < 0 || MCP_TitleListByDevice(handle, "odd", &discCount, discs, sizeof(discs)) < 0)
        discCount = 0;
    alignas(64) MCPTitleListType selected{};
    bool discMountAttempted[DiscSourceVolumeCount]{};
    auto target = chooseGameLaunch(install.game, install.sku,
        [&](const GameSku& sku, Device device) -> GameLaunchTarget {
            const auto id = sku.baseTitleId;
            alignas(64) MCPTitleListType record{};
            const auto matching = [&](const MCPTitleListType& title) {
                return title.titleId == id && deviceFromTitle(title) == device;
            };
            if (device == Device::Disc) {
                for (std::uint32_t i=0; i<std::min<std::uint32_t>(discCount,16); ++i) {
                    if (matching(discs[i])) { record=discs[i]; break; }
                }
            }
            if (!matching(record) && handle>=0) {
                if (MCP_GetTitleInfo(handle,id,&record)<0 || !matching(record)) record={};
            }

            const std::string reported(record.path,strnlen(record.path,sizeof(record.path)));
            if (device==Device::Disc) {
                if (!reported.empty() && !discSourceCandidates(reported,id).empty()) {
                    selected=record;
                    return {id,device,reported};
                }
                // The disc mount can be ready before MCP lists it. Verify
                // the base title ID from its read-only metadata in that case.
                for (unsigned volume=0; volume<DiscSourceVolumeCount; ++volume) {
                    const auto& source=DiscSourceVolumes[volume];
                    if (!discSourceMounted[volume] && !discMountAttempted[volume]) {
                        discMountAttempted[volume] = true;
                        discSourceMounted[volume] = Mocha_MountFS(source.alias, nullptr, source.root) == MOCHA_RESULT_SUCCESS;
                    }
                    if (!discSourceMounted[volume]) continue;
                    for (const auto& candidate : discSourceCandidates(source.root,id)) {
                        std::string root=candidate.root;
                        if (root.ends_with('/')) root.pop_back();
                        std::string xml;
                        std::uint16_t version=0;
                        if (!readTextFile(root+"/code/app.xml",xml,64*1024) ||
                            !stockTitleMetadata(xml,id,version)) continue;
                        selected=record;
                        return {id,device,std::string(source.root)+root.substr(std::strlen(source.alias)+1)};
                    }
                }
                return {};
            }

            const auto root=stockTitleRoot(device,id);
            const auto separator=root.find(':');
            const std::string rawPath="/vol/"+root.substr(0,separator)+root.substr(separator+1);
            if (lowerValue(reported)==rawPath) {
                selected=record;
                // Ghosts has a launcher RPX in both the base title and the
                // official update. Use the Menu handoff for its exact copy.
                return {id,device,sku.game==Game::Ghosts?reported:std::string{}};
            }
            InstallLocation verified;
            std::string ignored;
            if (!mountDevice(device) || probeStockTitle(device,id,verified,ignored)!=TitleProbe::Valid)
                return {};
            return {id,device,{}};
        });
    if (handle>=0) MCP_Close(handle);
    if (!target.path.empty() && selected.titleId==target.titleId) {
        const auto patchResult = ACPAssignTitlePatch(&selected);
        if (patchResult != ACP_RESULT_SUCCESS && target.device != Device::Disc)
            target.path.clear();
    }
    return target;
}

bool loadInstallPreferences(InstallPreferences& preferences, std::string& warning,
                            bool allowRecoveryWrites) {
    preferences = {};
    warning.clear();
    if (allowRecoveryWrites) {
        if (!recoverAtomicTextFile(config::gameInstallSettingsPath())) {
            warning = "The saved install settings need recovery. No game files will be changed.";
            return false;
        }
    } else if (exists(std::string(config::gameInstallSettingsPath()) + ".unbound-new") ||
               exists(std::string(config::gameInstallSettingsPath()) + ".unbound-old")) {
        warning = "The saved install settings need recovery, but HQ is read-only this session.";
        return false;
    }
    struct stat information{};
    if (stat(config::gameInstallSettingsPath(), &information) != 0) {
        if (errno == ENOENT) return true;
        warning = "Could not inspect the saved install settings. No game files will be changed.";
        return false;
    }
    if (!S_ISREG(information.st_mode)) {
        warning = "The saved install settings are not a readable file. No game files will be changed.";
        return false;
    }
    std::string text;
    if (!readTextFile(config::gameInstallSettingsPath(), text, config::MaxSettingsBytes)) {
        warning = "Could not read the saved install settings. No game files will be changed.";
        return false;
    }
    return parseInstallPreferences(text, preferences, warning);
}

bool saveInstallPreferences(const InstallPreferences& preferences, std::string& error) {
    error.clear();
    if (!validInstallPreferences(preferences)) {
        error = "The install destination or language setting is invalid";
        return false;
    }
    if (!recoverAtomicTextFile(config::gameInstallSettingsPath())) {
        error = "Could not recover an interrupted settings.ini update";
        return false;
    }
    std::string existing;
    struct stat information{};
    if (stat(config::gameInstallSettingsPath(), &information) == 0) {
        if (!S_ISREG(information.st_mode)) {
            error = "settings.ini is not a regular file";
            return false;
        }
        if (!readTextFile(config::gameInstallSettingsPath(), existing, config::MaxSettingsBytes)) {
            error = "Could not read settings.ini before saving the install settings";
            return false;
        }
    } else if (errno == ENOENT) {
        existing = "# Black Ops 2 Unbound HQ settings\n";
    } else {
        error = "Could not inspect settings.ini before saving the install settings";
        return false;
    }
    const std::string output = replaceInstallPreferences(existing, preferences);
    if (output.empty() || output.size() > config::MaxSettingsBytes) {
        error = "The saved settings exceed their safety limit";
        return false;
    }
    if (!writeTextFileAtomic(config::gameInstallSettingsPath(), output, error)) {
        error = "Could not save the install destination and language: " + error;
        return false;
    }
    return true;
}

bool scanInstallOptions(std::vector<InstallOption>& options, std::string& error,
                        const ProgressCallback& progress) {
    options.clear();
    error.clear();
    if (!mochaReady) {
        error = "Storage access is not ready";
        return false;
    }
    if (progress) progress("Install settings", "Checking internal storage and USB...", 8);
    bool mountedAny = false;
    for (const Device device : {Device::MLC, Device::USB}) {
        if (mountDevice(device)) mountedAny = true;
        else appendInstallWarning(error, std::string(deviceName(device)) +
                                  " is not connected, mounted, or readable.");
    }
    const std::string locale = currentInstallLocale();
    unsigned checked = 0;
    for (const Device device : {Device::MLC, Device::USB}) {
        const bool mounted = device == Device::USB ? usbMounted : mlcMounted;
        for (const auto& sku : gameSkus(config::activeGame)) {
            ++checked;
            if (!mounted) continue;
            if (progress) progress("Install settings", std::string("Checking ") +
                sku.label + " on " + deviceName(device) + "...",
                10 + static_cast<int>(checked) * 8);
            InstallOption option;
            option.install.sku = &sku;
            option.install.game = sku.game;
            std::string warning;
            const TitleProbe probe = probeStockTitle(device, sku.updateTitleId,
                                                    option.install.update, warning);
            if (probe == TitleProbe::Missing) continue;
            if (probe == TitleProbe::Invalid) {
                appendInstallWarning(error, warning);
                continue;
            }
            if (!scanInstallLanguages(option.install.update, option.languages, warning)) {
                appendInstallWarning(error, warning);
                continue;
            }
            const std::string preferred = languageFolderForSku(&sku, locale);
            const auto matching = std::find_if(option.languages.begin(), option.languages.end(),
                [&](const auto& language) {
                    return caseFoldedModFolder(language) == caseFoldedModFolder(preferred);
                });
            option.install.languageFolder = matching != option.languages.end() ? *matching :
                (option.languages.empty() ? std::string{} : option.languages.front());
            if (option.languages.empty()) {
                option.install.destinationReady = false;
                option.install.destinationWarning = "No supported language folders were found in " +
                    option.install.update.root + "/content.";
            }
            findPhysicalBase(option.install);
            if (!readSelectedInstallVersion(option.install, warning, false)) {
                option.install.destinationReady = false;
                option.install.destinationWarning = warning;
                appendInstallWarning(error, warning);
            }
            options.push_back(std::move(option));
        }
    }
    if (progress) progress("Install settings", options.empty() ?
        "No verified game update folders were found." :
        "Storage and language scan complete.", 100);
    return mountedAny;
}

bool applyInstallPreferences(GameInstall& install, const InstallPreferences& preferences,
                             std::string& error, const ProgressCallback& progress,
                             bool allowRecoveryWrites) {
    error.clear();
    const GameInstall original = install;
    install.destinationReady = false;
    install.destinationWarning.clear();
    install.targetOverridden = preferences.updateDevice != Device::Missing;
    install.languageOverridden = !preferences.languageFolder.empty();
    const auto fail = [&](const std::string& message) {
        install.destinationReady = false;
        install.destinationWarning = message;
        error = message;
        return false;
    };
    if (!validInstallPreferences(preferences))
        return fail("The saved install settings are invalid. Open Install settings and reset them to Auto.");

    std::vector<std::string> languages;
    if (install.targetOverridden) {
        const GameSku* selectedSku = findSkuByTitle(preferences.updateTitleId);
        // Valid preferences require an update title ID, not its base/AOC ID.
        if (!selectedSku || selectedSku->game != install.game ||
            selectedSku->updateTitleId != preferences.updateTitleId)
            return fail("The saved install destination is not a supported game update.");
        install.sku = selectedSku;
        install.update = {};
        install.update.device = preferences.updateDevice;
        install.update.root = titleRoot(preferences.updateDevice, preferences.updateTitleId);
        install.modVersion = "Not installed";
        if (original.sku != install.sku) {
            install.base = {};
            install.aoc = {};
        }
        std::vector<InstallOption> options;
        std::string scanWarning;
        scanInstallOptions(options, scanWarning, progress);
        const auto selected = std::find_if(options.begin(), options.end(), [&](const auto& option) {
            return option.install.sku->updateTitleId == preferences.updateTitleId &&
                   option.install.update.device == preferences.updateDevice;
        });
        if (selected == options.end()) {
            std::string message = "Your saved destination is unavailable: " + install.update.root +
                ". Connect that storage or choose another destination in Install settings. "
                "HQ will not switch storage automatically.";
            if (!scanWarning.empty()) message += "\n" + scanWarning;
            return fail(message);
        }
        install.update = selected->install.update;
        languages = selected->languages;
        findPhysicalBase(install, &original);
        if (original.sku != install.sku) install.aoc = {};
    } else {
        if (!install.sku || !install.update.present)
            return fail("No usable official game update was found. Install the official update or choose a verified destination in Install settings.");
        if (install.update.device != Device::MLC && install.update.device != Device::USB)
            return fail("The detected update is not on internal storage or USB.");
        if (install.update.root != titleRoot(install.update.device, install.sku->updateTitleId))
            return fail("The detected update path does not match the supported game title.");
        if (progress) progress("Install settings", "Verifying the detected update folder...", 90);
        if (!mountDevice(install.update.device)) {
            install.update.present = false;
            return fail("The detected update storage could not be mounted.");
        }
        InstallLocation verified;
        std::string warning;
        if (probeStockTitle(install.update.device, install.sku->updateTitleId,
                            verified, warning) != TitleProbe::Valid) {
            install.update.present = false;
            return fail(warning.empty() ? "The detected update folder is no longer present." : warning);
        }
        install.update = verified;
        if (!scanInstallLanguages(install.update, languages, warning)) return fail(warning);
    }
    if (languages.empty())
        return fail("No supported language folders exist in " + install.update.root +
                    "/content. HQ will not create a language folder automatically.");
    std::string preferred = preferences.languageFolder;
    if (preferred.empty()) {
        // Recompute Auto from the console locale, not a previously overridden
        // language on the same SKU. Resetting French to Auto must really reset.
        preferred = languageFolderForSku(install.sku, currentInstallLocale());
    }
    const auto matching = std::find_if(languages.begin(), languages.end(), [&](const auto& language) {
        return caseFoldedModFolder(language) == caseFoldedModFolder(preferred);
    });
    if (matching != languages.end()) install.languageFolder = *matching;
    else if (install.languageOverridden)
        return fail("Your saved language folder '" + preferences.languageFolder +
            "' no longer exists in " + install.update.root +
            "/content. Choose an available language or reset Language to Auto in Install settings.");
    else install.languageFolder = languages.front();

    std::string markerError;
    if (!readSelectedInstallVersion(install, markerError, allowRecoveryWrites)) return fail(markerError);
    install.destinationReady = true;
    if (progress) progress("Install settings", std::string("Using ") +
        deviceName(install.update.device) + " / " + install.languageFolder + ".", 100);
    return true;
}

bool readTextFile(const std::string& path, std::string& output, std::size_t maximum) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    output.clear(); char buffer[4096]; bool ok = true;
    while (auto count = std::fread(buffer, 1, sizeof(buffer), file)) {
        if (output.size() + count > maximum) { ok = false; break; }
        output.append(buffer, count);
    }
    if (std::ferror(file)) ok = false;
    std::fclose(file);
    return ok;
}

bool writeTextFileAtomic(const std::string& path, const std::string& value, std::string& error) {
    if (!ensureDirectory(parentPath(path))) { error = "Could not create destination directory"; return false; }
    if (!recoverAtomicTextFile(path)) { error = "Could not recover interrupted file state"; return false; }
    const std::string temporary = path + ".unbound-new";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) { error = "Could not create temporary file"; return false; }
    bool ok = std::fwrite(value.data(), 1, value.size(), file) == value.size();
    if (ok && std::fflush(file) != 0) ok = false;
    if (ok && fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (!ok) { std::remove(temporary.c_str()); error = "Could not write temporary file"; return false; }
    const std::string previous = path + ".unbound-old";
    std::remove(previous.c_str());
    const bool hadPrevious = exists(path);
    if (hadPrevious && std::rename(path.c_str(), previous.c_str()) != 0) {
        std::remove(temporary.c_str()); error = "Could not preserve previous file"; return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        if (hadPrevious) std::rename(previous.c_str(), path.c_str());
        std::remove(temporary.c_str()); error = "Could not commit file"; return false;
    }
    if (hadPrevious) std::remove(previous.c_str());
    return true;
}

bool downloadText(const std::string& url, std::string& output, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) { error = "Could not create HTTP request"; return false; }
    output.clear();
    if (!configureCurl(curl, url, error)) { curl_easy_cleanup(curl); return false; }
    char curlError[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    CURLcode result = CURLE_OK;
    for (int attempt = 0; attempt < 3; ++attempt) {
        output.clear();
        curlError[0] = '\0';
        result = curl_easy_perform(curl);
        if (result != CURLE_COULDNT_RESOLVE_HOST && result != CURLE_COULDNT_RESOLVE_PROXY) break;
        if (attempt < 2) OSSleepTicks(OSMillisecondsToTicks(750 * (attempt + 1)));
    }
    bool ok = curlResult(curl, result, curlError, error);
    curl_easy_cleanup(curl);
    return ok;
}

bool downloadFile(const std::string& url, const std::string& destination, std::string& error,
                  const ProgressCallback& progress, unsigned long long maximumBytes,
                  const std::string* expectedSha256, DownloadControl* control,
                  unsigned long long expectedBytes) {
    const auto operationBegan = operationMetricsNow();
    error.clear();
    const auto cancelledBeforeTransfer = [&]() {
        if (!downloadCancellationRequested(control)) return false;
        control->markCancelled();
        error = "Download cancelled.";
        return true;
    };
    if (cancelledBeforeTransfer()) return false;
    std::string expectedHash=expectedSha256?*expectedSha256:std::string{};
    if(expectedSha256&&(expectedHash.size()!=64||std::any_of(expectedHash.begin(),expectedHash.end(),[](unsigned char c){return !std::isxdigit(c);}))) {
        error="Manifest is missing a valid SHA-256";return false;
    }
    if(expectedBytes&&(!expectedSha256||expectedBytes>maximumBytes)){
        error="Manifest contains an invalid package size";return false;
    }
    std::transform(expectedHash.begin(),expectedHash.end(),expectedHash.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (!ensureDirectory(parentPath(destination))) { error = "Could not create download directory"; return false; }
    const std::string temporary = destination + ".part";
    const std::string identity=expectedSha256?resumeRequestKey(url,expectedHash):std::string{};
    ResumeRecord saved;
    bool resumeReady=expectedSha256&&readResumePair(temporary,maximumBytes,saved)&&saved.requestKey==identity;
    if(!resumeReady)removeResumeRecord(temporary);
    unsigned long long resumeOffset=resumeReady?saved.bytes:0;
    constexpr unsigned long long Reserve = 8ULL * 1024 * 1024;
    unsigned long long available = 0;
    if (!freeSpaceForPath(destination, available) || available <= Reserve) {
        error = "Could not reserve safe free space for the download";
        return false;
    }
    const auto extra=std::min(resumeOffset,std::numeric_limits<unsigned long long>::max()-(available-Reserve));
    const unsigned long long writableLimit = std::min(maximumBytes, available - Reserve + extra);
    CURL* curl = curl_easy_init();
    if (!curl) { error = "Could not create HTTP request"; return false; }
    std::uint64_t probeTicks = 0;
    const auto size = resolveDownloadSize(expectedBytes, maximumBytes, expectedSha256 != nullptr,
        [&](unsigned long long& detected) {
            const auto began = operationMetricsNow();
            const bool known = remoteFileSize(curl, url, detected, progress, control);
            probeTicks = operationMetricsNow() - began;
            return known;
        });
    if(!size.valid){curl_easy_cleanup(curl);error="Manifest contains an invalid package size";return false;}
    unsigned long long expectedSize = size.bytes;
    const bool sizeKnown = size.known;
    if (cancelledBeforeTransfer()) { curl_easy_cleanup(curl); return false; }
    if (sizeKnown) {
        if (expectedSize > writableLimit) { curl_easy_cleanup(curl); error = "Not enough SD space for this download"; return false; }
        if(resumeReady&&((saved.totalBytes&&saved.totalBytes!=expectedSize)||resumeOffset>expectedSize)) {
            resumeReady=false;resumeOffset=0;removeResumeRecord(temporary);
        }
    } else if (writableLimit < std::min<unsigned long long>(maximumBytes, 16ULL * 1024 * 1024)) {
        curl_easy_cleanup(curl);error = "Not enough safely reserved SD space for this download";
        return false;
    }
    if(!expectedSize&&resumeReady)expectedSize=saved.totalBytes;
    Sha256 streamedHash;
    std::uint64_t rehashTicks=0;
    if(resumeReady) {
        const auto began=operationMetricsNow();
        resumeReady=hashResumePrefix(temporary,saved,streamedHash,progress,control);
        rehashTicks=operationMetricsNow()-began;
        // A cancelled prefix check has not changed the file or sidecar. Keep
        // both so the next attempt can finish verifying the saved prefix.
        if (cancelledBeforeTransfer()) { curl_easy_cleanup(curl); return false; }
        if(!resumeReady){resumeOffset=0;streamedHash=Sha256{};removeResumeRecord(temporary);}
    }
    AlignedIoBuffer fileBuffer;
    FILE* file = std::fopen(temporary.c_str(), resumeReady?"ab":"wb");
    if (!file) { curl_easy_cleanup(curl);error = "Could not create download file"; return false; }
    if (fileBuffer)
        (void)std::setvbuf(file, fileBuffer.data(), _IOFBF, fileBuffer.size());
    curl_easy_reset(curl);
    if (!configureCurl(curl, url, error)) { curl_easy_cleanup(curl); std::fclose(file); return false; }
    char curlError[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendFile);
    curl_easy_setopt(curl,CURLOPT_MAXFILESIZE_LARGE,static_cast<curl_off_t>(writableLimit));
    FileDownload download{file,writableLimit,resumeOffset,false,expectedSha256?&streamedHash:nullptr};
    download.accepted=resumeOffset;
    download.control=control;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    TransferProgress transfer{&progress,expectedSize,{}};
    transfer.resumeOffset=resumeOffset;
    transfer.control=control;
    ResumeResponse response(resumeOffset,writableLimit,expectedSize);
    const auto configureResume=[&](){
        if(resumeOffset){
            download.resumeResponse=&response;
            curl_easy_setopt(curl,CURLOPT_RESUME_FROM_LARGE,static_cast<curl_off_t>(resumeOffset));
            curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,receiveResumeHeader);
            curl_easy_setopt(curl,CURLOPT_HEADERDATA,&response);
        }else{
            download.resumeResponse=nullptr;
            curl_easy_setopt(curl,CURLOPT_RESUME_FROM_LARGE,static_cast<curl_off_t>(0));
            curl_easy_setopt(curl,CURLOPT_HEADERFUNCTION,nullptr);
            curl_easy_setopt(curl,CURLOPT_HEADERDATA,nullptr);
        }
    };
    configureResume();
    if (progress || control) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportTransfer);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    }
    CURLcode result = CURLE_OK;
    bool retryResetFailed = false;
    bool restartedFresh=false;
    unsigned dnsRetries=0;
    Sha256 initialHash=streamedHash;
    const bool completeSaved=resumeReady&&expectedSize&&resumeOffset==expectedSize;
    for (;;) {
        if (downloadCancellationRequested(control)) { result=CURLE_ABORTED_BY_CALLBACK; break; }
        if(completeSaved)break;
        curlError[0] = '\0';
        result = performFileTransfer(curl, transfer, download);
        if (downloadCancellationRequested(control)) break;
        const bool restartFresh=resumeOffset&&!restartedFresh&&response.restartRequired();
        const bool retryDns=(result==CURLE_COULDNT_RESOLVE_HOST||result==CURLE_COULDNT_RESOLVE_PROXY)&&dnsRetries<2;
        if(!restartFresh&&!retryDns)break;
        if(restartFresh){
            restartedFresh=true;resumeOffset=0;initialHash=Sha256{};
            removeResumeRecord(temporary);
        }else ++dnsRetries;
        {
            bool resetOk = std::fflush(file) == 0;
            if (ftruncate(fileno(file),static_cast<off_t>(resumeOffset)) != 0) resetOk = false;
            if (std::fseek(file, 0, SEEK_END) != 0) resetOk = false;
            std::clearerr(file);
            if (!resetOk) {
                retryResetFailed = true;
                error = "Could not safely reset the partial download for retry";
                break;
            }
            download.received = resumeOffset;
            download.accepted = resumeOffset;
            download.limitExceeded = false;
            download.writeFailed = false;
            if(download.hash)streamedHash=initialHash;
            transfer.eta.reset();
            transfer.resumeOffset=resumeOffset;
            response.reset();
            configureResume();
        }
        if(progress)progress("Downloading package",restartFresh?"Server cannot resume this download; restarting safely...":"DNS was not ready; retrying connection...",0);
        if (downloadCancellationRequested(control)) { result=CURLE_ABORTED_BY_CALLBACK; break; }
        if(!restartFresh)OSSleepTicks(OSMillisecondsToTicks(750*dnsRetries));
    }
    long responseCode=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&responseCode);
    if(download.resumeResponse&&response.bodyAllowed()&&response.totalBytes())expectedSize=response.totalBytes();
    const bool userCancelled = downloadCancellationRequested(control);
    bool ok = !userCancelled && !retryResetFailed && (completeSaved||curlResult(curl, result, curlError, error));
    if (userCancelled) error = "Download cancelled.";
    if (!ok && download.limitExceeded) error = "Download stopped before exhausting reserved SD space";
    else if (!ok && download.writeFailed) error = "Could not write the download to SD";
    if (ok && download.received != download.accepted) {
        ok = false; error = "The download writer did not finish every received byte";
    }
    const auto closeBegan = operationMetricsNow();
    const bool closedCleanly=std::fclose(file)==0;
    if (!closedCleanly){ok=false;error="Could not finish writing the download";}
    download.writeTicks += operationMetricsNow() - closeBegan;
    curl_easy_cleanup(curl);
    // Only acknowledge a normal cancellation after FILE and both workers are
    // closed. A disk failure still surfaces as a failure rather than hiding it.
    if (userCancelled && closedCleanly && !download.writeFailed &&
        !download.limitExceeded && !retryResetFailed)
        control->markCancelled();
    bool retainedMetadataMismatch=false;
    if(ok&&expectedSha256){
        Sha256 completeHash=streamedHash;
        const FileHashResult streamed{FileHashStatus::Ok,0,download.received,download.received};
        const auto integrity=classifyPackageIntegrity(streamed,completeHash.finish(),expectedHash,expectedBytes);
        if(integrity==PackageIntegrityDecision::MetadataMismatch){
            error="Release metadata has the wrong package size, but the downloaded ZIP matches the published SHA-256. "
                "A corrected release record is needed; downloading again cannot fix this.\nDeclared: "+
                std::to_string(expectedBytes)+" bytes; verified: "+std::to_string(download.received)+" bytes.";
            // Preserve only the canonical, hash-owned core cache. Return failure
            // after committing it below, so no installation can start with
            // inconsistent release metadata. Other download types stay strict.
            retainedMetadataMismatch=destination==std::string(config::gameCacheRoot())+"/core-"+expectedHash+".zip";
            if(!retainedMetadataMismatch)ok=false;
        }else if(integrity!=PackageIntegrityDecision::Valid){
            ok=false;error="Package SHA-256 does not match the manifest";
        }
    }
    if(ok&&!retainedMetadataMismatch&&expectedSize&&download.received!=expectedSize){
        ok=false;error="The downloaded file did not match its expected size";
    }
    const auto logResult = [&](bool success) {
        appendPerformanceLog("download", success,
            "total_ms=" + std::to_string(operationMetricsMilliseconds(operationMetricsNow() - operationBegan)) +
            "|curl_ms=" + std::to_string(operationMetricsMilliseconds(download.curlTicks)) +
            "|write_ms=" + std::to_string(operationMetricsMilliseconds(download.writeTicks)) +
            "|hash_ms=" + std::to_string(operationMetricsMilliseconds(download.hashTicks)) +
            "|queue_wait_ms=" + std::to_string(operationMetricsMilliseconds(download.queueWaitTicks)) +
            "|probe_ms=" + std::to_string(operationMetricsMilliseconds(probeTicks)) +
            "|size_source=" + (size.probed ? (size.known ? "head" : "unknown") : "manifest") +
            "|rehash_ms=" + std::to_string(operationMetricsMilliseconds(rehashTicks)) +
            "|resumed_bytes=" + std::to_string(resumeOffset) +
            "|network_bytes=" + std::to_string(download.networkBytes) +
            "|metadata_mismatch=" + (retainedMetadataMismatch ? "1" : "0") +
            "|cancelled=" + (control && control->cancelled() ? "1" : "0") +
            "|bytes=" + std::to_string(download.received) +
            "|mode=" + (download.usedPipeline ? "pipeline" : "buffered"));
    };
    if (!ok) {
        // Nothing extra is written on a successful fresh transfer. A sidecar is
        // produced only after a recoverable interruption and after both workers
        // have joined and the buffered file has closed successfully.
        const bool validBody=responseCode==200||
            (responseCode==206&&download.resumeResponse&&response.bodyAllowed());
        bool retained=false;
        if(retainInterruptedDownload(expectedSha256 != nullptr, closedCleanly,
           download.writeFailed, download.limitExceeded, retryResetFailed,
           userCancelled || resumableNetworkFailure(result), download.received,
           validBody, resumeOffset)){
            Sha256 prefix=streamedHash;
            ResumeRecord record{identity,prefix.finish(),download.received,expectedSize};
            struct stat info{};
            std::string saveError;
            const auto text=serializeResumeRecord(record,maximumBytes);
            retained=!text.empty()&&stat(temporary.c_str(),&info)==0&&S_ISREG(info.st_mode)&&info.st_size>0&&
                static_cast<unsigned long long>(info.st_size)==record.bytes&&
                writeTextFileAtomic(temporary+".resume",text,saveError);
        }
        if(retained)error+="\n\nSaved download progress. Retry this same release to resume instead of starting over.";
        else{std::remove(temporary.c_str());removeResumeRecord(temporary);}
        logResult(false);return false;
    }
    std::remove(destination.c_str());
    if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
        std::remove(temporary.c_str());removeResumeRecord(temporary); error = "Could not commit download"; logResult(false); return false;
    }
    removeResumeRecord(temporary);
    logResult(!retainedMetadataMismatch);
    return !retainedMetadataMismatch;
}

namespace {
bool validHqWuhb(const std::string& path) {
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 4)
        return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    char magic[4]{};
    const bool ok = std::fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
                    std::memcmp(magic, "WUHB", sizeof(magic)) == 0;
    std::fclose(file);
    return ok;
}

// Copy only settings and install records when both application folders exist.
bool mergeHqStateEntry(const std::string& source, const std::string& target,
                       unsigned depth, std::string& error) {
    const auto fail = [&] {
        error = "Could not preserve HQ state at " + target + ". The old app files were kept.";
        return false;
    };
    struct stat from{}, to{};
    if (lstat(source.c_str(), &from) != 0) return errno == ENOENT || fail();
    const bool targetExists = lstat(target.c_str(), &to) == 0;
    if (!targetExists && errno != ENOENT) return fail();
    if (S_ISREG(from.st_mode)) {
        if (targetExists) return S_ISREG(to.st_mode) || fail();
        const auto stage = target + ".hq-migration";
        std::string data;
        if (!readTextFile(source, data, 2 * 1024 * 1024) ||
            !writeTextFileAtomic(stage, data, error) ||
            lstat(target.c_str(), &to) == 0 || errno != ENOENT ||
            std::rename(stage.c_str(), target.c_str()) != 0)
            return fail();
        return true;
    }
    if (!S_ISDIR(from.st_mode) || depth == 0 ||
        (targetExists ? !S_ISDIR(to.st_mode) : !ensureDirectory(target)))
        return fail();
    DIR* directory = opendir(source.c_str());
    if (!directory) return fail();
    bool ok = true;
    while (ok) {
        errno = 0;
        const auto* entry = readdir(directory);
        if (!entry) {
            if (errno) ok = fail();
            break;
        }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        ok = mergeHqStateEntry(source + "/" + name, target + "/" + name, depth - 1, error);
    }
    closedir(directory);
    return ok;
}

bool releaseRunningHqBundle(std::string& error) {
    auto status = RPXLoader_InitLibrary();
    if (status == RPX_LOADER_RESULT_SUCCESS) {
        status = RPXLoader_UnmountCurrentRunningBundle();
        RPXLoader_DeInitLibrary();
    }
    if (status == RPX_LOADER_RESULT_SUCCESS) return true;
    error = "Could not release the running HQ bundle: " + std::string(RPXLoader_GetStatusStr(status));
    return false;
}
} // namespace

AppPathMigration migrateApplicationPath(std::string& error) {
    if constexpr (config::DiagnosticSdBuild) return AppPathMigration::Unchanged;
    constexpr const char* oldName = "/BO2-Unbound-HQ.wuhb";
    const auto oldPath = std::string(config::LegacyHqSdRoot) + oldName;
    const auto intermediate = std::string(config::PublishedSdRoot) + oldName;
    const auto renamedLegacy = std::string(config::LegacyHqSdRoot) + "/" + config::AppWuhbName;
    if (!exists(oldPath) && !exists(intermediate) && !exists(renamedLegacy))
        return AppPathMigration::Unchanged;

    char running[512]{};
    auto status = RPXLoader_InitLibrary();
    if (status == RPX_LOADER_RESULT_SUCCESS) {
        status = RPXLoader_GetPathOfRunningExecutable(running, sizeof(running)-1);
        RPXLoader_DeInitLibrary();
    }
    if (status != RPX_LOADER_RESULT_SUCCESS) {
        error = "HQ found the old app name, but Aroma could not identify which copy is running: " +
                std::string(RPXLoader_GetStatusStr(status));
        return AppPathMigration::Failed;
    }
    struct Io {
        bool validWuhb(const std::string& path) { return validHqWuhb(path); }
        bool missing(const std::string& path) {
            struct stat info{};
            return lstat(path.c_str(), &info) != 0 && errno == ENOENT;
        }
        bool mergeState(const std::string& oldRoot, const std::string& newRoot, std::string& failure) {
            struct stat root{};
            if (lstat(newRoot.c_str(), &root) != 0 || !S_ISDIR(root.st_mode)) {
                failure = "Could not open the Unbound-HQ folder. The old app files were kept.";
                return false;
            }
            for (const char* entry : {"/settings.ini", "/settings.ini.unbound-new", "/settings.ini.unbound-old",
                                      "/state", "/ghosts/settings.ini", "/ghosts/settings.ini.unbound-new",
                                      "/ghosts/settings.ini.unbound-old", "/ghosts/state"}) {
                if (!mergeHqStateEntry(oldRoot + entry, newRoot + entry, 4, failure)) return false;
            }
            return true;
        }
        bool release(std::string& failure) { return releaseRunningHqBundle(failure); }
        bool rename(const std::string& from, const std::string& to, std::string& failure) {
            if (missing(to) && std::rename(from.c_str(), to.c_str()) == 0) return true;
            const int code = errno;
            failure = "Could not rename " + from + " to " + to +
                      " (" + std::to_string(code) + ": " + std::strerror(code) + ").";
            return false;
        }
    } io;
    return migrateHqPath(running, io, error);
}

bool installApplicationUpdate(const std::string& stagedWuhb, std::string& error) {
    const std::string target = config::AppWuhbPath;
    const std::string backup = config::AppBackupPath;
    const std::string pendingBackup = backup + ".pending";
    if (stagedWuhb != config::AppUpdateStagingPath || parentPath(stagedWuhb) != parentPath(target)) {
        error = "The HQ update was not staged beside the installed application";
        return false;
    }
    if (!validHqWuhb(target)) {
        error = "The canonical HQ WUHB was not found. Reinstall HQ at " + target;
        return false;
    }
    if (!validHqWuhb(stagedWuhb)) {
        error = "The downloaded file is not a valid WUHB application";
        return false;
    }
    // Aroma keeps the currently running WUHB mounted as its executable/content
    // bundle. The file cannot be renamed until RPXLoadingModule releases that
    // mount. This is the same sequence used by the official Aroma Updater.
    if (!releaseRunningHqBundle(error)) return false;
    if (exists(pendingBackup) && std::remove(pendingBackup.c_str()) != 0) {
        const int code = errno;
        error = "Could not clear an interrupted HQ backup (" +
                std::to_string(code) + ": " + std::strerror(code) + ")";
        return false;
    }
    // Keep the established recovery backup untouched until the verified new
    // WUHB is already present at the canonical path.
    if (std::rename(target.c_str(), pendingBackup.c_str()) != 0) {
        const int code = errno;
        error = "Could not preserve the currently installed HQ application (" +
                std::to_string(code) + ": " + std::strerror(code) + ")";
        return false;
    }
    if (std::rename(stagedWuhb.c_str(), target.c_str()) != 0) {
        const int code = errno;
        const bool restored = std::rename(pendingBackup.c_str(), target.c_str()) == 0;
        error = restored
            ? "Could not commit the HQ update; the previous application was restored (" +
              std::to_string(code) + ": " + std::strerror(code) + ")"
            : "Could not commit or restore HQ automatically. On a PC, rename "
              + pendingBackup + " to " + target + " before relaunching HQ";
        return false;
    }
    // The new canonical WUHB is now committed. Rotate the immediate predecessor
    // into the documented recovery name without ever deleting the only working
    // canonical application. If backup rotation fails, leave pendingBackup in
    // place as a recoverable copy; the successful update remains bootable.
    if (!exists(backup) || std::remove(backup.c_str()) == 0)
        (void)std::rename(pendingBackup.c_str(), backup.c_str());
    return true;
}

bool verifyDownloadedPackage(const std::string& archive, const std::string& expected, std::string& error) {
    return checkDownloadedPackage(archive,expected,0,error)==PackageVerificationStatus::Valid;
}

PackageVerificationStatus checkDownloadedPackage(const std::string& archive,
    const std::string& expected,unsigned long long expectedBytes,std::string& error,
    const ProgressCallback& progress,DownloadControl* control) {
    error.clear();
    if(expected.size()!=64||!std::all_of(expected.begin(),expected.end(),[](unsigned char ch){return std::isxdigit(ch);})){
        error="Manifest is missing a valid SHA-256";return PackageVerificationStatus::ReadFailure;
    }
    const auto began=operationMetricsNow();
    auto status=PackageVerificationStatus::ReadFailure;
    FileHashResult result;
    InstallActivity activity(SDL_GetTicks());
    const ProgressCallback display=progress?ProgressCallback{[&](const std::string& title,const std::string& detail,int percent){
        const auto now=SDL_GetTicks();activity.observe(title,detail,percent,now);
        const auto frame=activity.snapshot(now);progress(frame.title,frame.detail,frame.percent);
    }}:ProgressCallback{};
    const BackgroundTickCallback tick=(progress||control)?BackgroundTickCallback{[&]{
        if(control)control->pollInput();
        else if(operationFrameCallback)operationFrameCallback();
        const auto now=SDL_GetTicks();
        if(progress&&activity.pulseDue(now)){
            const auto frame=activity.snapshot(now);progress(frame.title,frame.detail,frame.percent);
        }
    }}:BackgroundTickCallback{};
    std::string lower = expected;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    runBackgroundOperation("HQ package verification",display,[&](const ProgressCallback& relay){
        if(relay)relay("Checking saved package","Opening the saved ZIP for a read-only integrity check...",-1);
        EtaState eta;
        FileHashOptions options;
        // Read the complete inspected file before comparing feed size. A
        // correct digest with a mistyped feed size is a publishing issue,
        // not a corrupt cache that another download could repair.
        options.expectedBytes=0;
        options.cancelled=[control]{return downloadCancellationRequested(control);};
        options.progress=[&](std::uint64_t checked,std::uint64_t total){
            if(!relay)return;
            const auto now=SDL_GetTicks();eta.observe(checked,now);
            const int percent=total?static_cast<int>((checked*100ULL)/total):0;
            if(!eta.shouldRender(percent,now))return;
            std::string detail="Verified "+dataAmount(checked)+" of "+dataAmount(total)+"\n"+eta.remaining(checked,total,now);
            const auto rate=measuredDownloadRate(eta.smoothedRate);
            if(!rate.empty())detail+="  |  "+rate;
            relay("Checking saved package",detail,percent);
        };
        std::string actual;
        result=sha256FileDetailed(archive,actual,options);
        const auto decision=classifyPackageIntegrity(result,actual,lower,expectedBytes);
        if(decision==PackageIntegrityDecision::Cancelled){
            if(control)control->markCancelled();
            error="Verification cancelled. The saved ZIP and install repair record were retained.";
            status=PackageVerificationStatus::Cancelled;
        }else if(!result.ok()){
            const bool invalid=result.status==FileHashStatus::SizeMismatch||result.status==FileHashStatus::Truncated||result.status==FileHashStatus::SizeChanged;
            status=invalid?PackageVerificationStatus::InvalidContent:PackageVerificationStatus::ReadFailure;
            switch(result.status){
            case FileHashStatus::StatFailed:error="Could not inspect the saved ZIP on SD.";break;
            case FileHashStatus::NotRegular:error="The saved ZIP path is not a regular file.";break;
            case FileHashStatus::OpenFailed:error="Could not open the saved ZIP on SD.";break;
            case FileHashStatus::SizeMismatch:error="The saved ZIP has the wrong size.";break;
            case FileHashStatus::BufferUnavailable:error="Not enough memory to verify the saved ZIP.";break;
            case FileHashStatus::BufferSetupFailed:error="Could not prepare the SD read buffer.";break;
            case FileHashStatus::ReadFailed:error="An SD read failed while checking the saved ZIP.";break;
            case FileHashStatus::Truncated:error="The saved ZIP ended before all expected bytes were read.";break;
            case FileHashStatus::SizeChanged:error="The saved ZIP changed size while being checked.";break;
            case FileHashStatus::CloseFailed:error="Could not finish closing the saved ZIP after reading.";break;
            default:error="Could not verify the saved ZIP.";break;
            }
            error+="\nChecked "+dataAmount(result.bytes)+" of "+dataAmount(result.total)+".";
            if(expectedBytes)error+=" Expected package size: "+dataAmount(expectedBytes)+".";
            if(result.errorNumber)error+="\nError "+std::to_string(result.errorNumber)+" ("+std::strerror(result.errorNumber)+").";
        }else if(decision==PackageIntegrityDecision::MetadataMismatch){
            status=PackageVerificationStatus::MetadataMismatch;
            error="Release metadata has the wrong package size, but the saved ZIP matches the published SHA-256. "
                "The download was kept. A corrected release record is needed; downloading again cannot fix this.\n"
                "Declared: "+std::to_string(expectedBytes)+" bytes; verified: "+std::to_string(result.total)+" bytes.";
        }else if(decision!=PackageIntegrityDecision::Valid){
            status=PackageVerificationStatus::InvalidContent;
            error="Package SHA-256 does not match the manifest.\nExpected: "+lower+"\nRead from SD: "+actual;
        }else status=PackageVerificationStatus::Valid;
        return status==PackageVerificationStatus::Valid;
    },nullptr,tick);
    // The caller polls input once more after joining. Honor a final B press
    // before a valid digest can authorize any subsequent game-file writes.
    if(status==PackageVerificationStatus::Valid&&downloadCancellationRequested(control)){
        control->markCancelled();status=PackageVerificationStatus::Cancelled;
        error="Verification cancelled. The saved ZIP and install repair record were retained.";
    }
    appendPerformanceLog("verify",status==PackageVerificationStatus::Valid,
        "total_ms="+std::to_string(operationMetricsMilliseconds(operationMetricsNow()-began))+
        "|bytes="+std::to_string(result.bytes)+"|total_bytes="+std::to_string(result.total)+
        "|status="+std::to_string(static_cast<int>(status))+"|io_status="+
        std::to_string(static_cast<int>(result.status))+"|errno="+std::to_string(result.errorNumber));
    return status;
}

bool resetCorePackageDownloadCache(const std::string& sha256,std::string& error) {
    struct Io {
        CoreRecoveryCacheFile inspectCache(const std::string& path){
            struct stat info{};
            if(lstat(path.c_str(),&info)==0)return S_ISREG(info.st_mode)?CoreRecoveryCacheFile::Regular:CoreRecoveryCacheFile::Other;
            return errno==ENOENT?CoreRecoveryCacheFile::Missing:CoreRecoveryCacheFile::Error;
        }
        bool removeCache(const std::string& path,std::string& failure){
            if(std::remove(path.c_str())==0||errno==ENOENT)return true;
            failure="Could not clear this package's saved download: "+path+" ("+std::strerror(errno)+")";return false;
        }
    } io;
    return clearCorePackageCacheWithIo(sha256,io,error);
}

bool installCoreArchive(const std::string& archive, const GameInstall& install,
                        const std::string& newVersion, std::string& error,
                        const ProgressCallback& progress,
                        const std::function<bool(std::string&)>& beforeMutation) {
    if (!install.update.present || !install.destinationReady) {
        error = install.destinationWarning.empty() ?
            "Choose a valid install destination and language before installing." :
            install.destinationWarning;
        return false;
    }
    // Recheck after downloading, immediately before extraction. A disconnected
    // drive or moved title must not cause ensureDirectory() to recreate a stale
    // destination. These are small metadata reads, never a recursive file scan.
    if (!revalidateInstallDestination(install, error, true)) return false;
    const auto marker = install.update.root + "/" + config::VersionRelativePath;
    return extractArchive(archive, coreMapper, const_cast<GameInstall*>(&install), error,
                          progress, [&marker, &newVersion](std::string& commitError) {
                              std::string previous;
                              if(!readInstallState(marker,previous,commitError))return false;
                              return writeTextFileAtomic(marker, markerWithCore(previous,newVersion), commitError);
                          }, {}, beforeMutation);
}

bool readInstallState(const std::string& path,std::string& text,std::string& error){
    text.clear();
    if(!recoverAtomicTextFile(path)){error="Could not recover saved install state: "+path;return false;}
    struct stat st{};
    if(lstat(path.c_str(),&st)!=0){if(errno==ENOENT)return true;error="Could not inspect saved install state";return false;}
    if(!S_ISREG(st.st_mode)||!readTextFile(path,text,1024*1024)){error="Could not read saved install state: "+path;return false;}
    return true;
}

bool retireModLink(bool& changed,std::string& error){
    changed=false;
    const std::string relative="wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps";
    const std::string path="fs:/vol/external01/"+relative;
    struct stat st{};
    if(lstat(path.c_str(),&st)!=0){if(errno==ENOENT)return true;error="Could not inspect the retired plugin";return false;}
    if(!S_ISREG(st.st_mode)){error="The old plugin path is not a normal file. Remove it manually after closing HQ.";return false;}
    std::string hash;if(!sha256File(path,hash)){error="Could not verify the old plugin before retiring it.";return false;}
    bool known=hash=="04f6f61684ac332496d05c9c66204bc0158b48d7d6c16881be968a06f95eb180";
    WorkshopPlugin plugin;plugin.key="UNBOUND_MOD_LINK";plugin.managedPaths={relative};
    PluginRelease receipt;bool found=false;std::string receiptError;
    if(loadPluginReceipt(plugin,receipt,found,receiptError)&&found)
        for(const auto& file:receipt.files)if(file.archivePath==relative&&file.sha256==hash&&file.bytes==std::uint64_t(st.st_size))known=true;
    if(!known){error="An unrecognized BO2-Unbound-Redirector.wps was left untouched. Remove it from Aroma plugins manually and restart the console before playing.";return false;}
    if(!writeTextFileAtomic(std::string(config::SdRoot)+"/state/retirement-restart.txt","Restart required\n",error))return false;
    if(!writeTextFileAtomic(config::ModLinkDisableFlagPath,"Retired: SD content is loaded by Unbound Core.\n",error))return false;
    const std::string backup=std::string(config::SdRoot)+"/retired-plugins/"+hash+".wps.disabled";
    if(!ensureDirectory(parentPath(backup))){error="Could not create the retired-plugin backup folder";return false;}
    struct stat saved{};
    if(lstat(backup.c_str(),&saved)==0){
        std::string savedHash;
        if(!S_ISREG(saved.st_mode)||!sha256File(backup,savedHash)||savedHash!=hash){error="Retired-plugin backup needs attention";return false;}
        if(std::remove(path.c_str())!=0){error="Could not remove the verified duplicate old plugin";return false;}
    }else if(errno!=ENOENT||std::rename(path.c_str(),backup.c_str())!=0){error="Could not move the retired plugin out of Aroma";return false;}
    changed=true;return true;
}

bool installMapArchive(const std::string& archive, const GameInstall& install,
                       const std::string& id, const std::string& version,
                       std::string& error, const ProgressCallback& progress,
                       const std::function<bool(std::string&)>& beforeMutation) {
    if(!packId(id)||!mapPackTargetReady(install)){error="Choose an available Core update destination first.";return false;}
    if(!revalidateInstallDestination(install,error,true))return false;
    struct MapContext {
        const GameInstall& game;
        bool modPackage;
        std::set<std::string> otherFiles;
        std::vector<std::pair<std::string,std::string>> paths;
    } context{install,id.rfind("mod-",0)==0,{},{}};
    const auto stateRoot=std::string(config::gameStateRoot())+"/maps";
    const auto device=install.update.device;
    const auto ownershipSuffix=mapOwnershipSuffix(device);
    if(!ensureDirectory(stateRoot)){error="Could not open map installation records";return false;}
    DIR* records=opendir(stateRoot.c_str());
    if(!records){error="Could not inspect installed map ownership";return false;}
    while(auto* entry=readdir(records)){
        const std::string name=entry->d_name;
        if((!name.ends_with("~usb.files")&&!name.ends_with("~nand.files"))||name.rfind(id+"~",0)==0)continue;
        const bool sameDevice=name.ends_with(ownershipSuffix);
        std::string saved;
        if(!readTextFile(stateRoot+"/"+name,saved,1024*1024)){closedir(records);error="Could not read map file ownership";return false;}
        std::istringstream lines(saved);std::string path;
        while(std::getline(lines,path))if(sameDevice||path.rfind("sd:",0)==0)context.otherFiles.insert(lowerValue(trim(path)));
    }
    closedir(records);
    const auto mapper=[](const std::string& name,std::string& target,std::string& logical,void* opaque){
        auto& state=*static_cast<MapContext*>(opaque);
        if(state.modPackage){
            const auto route=modPackageRoute(name,state.game.languageFolder);
            if(route.area==PackageArea::Invalid)return false;
            const auto key=modPackageOwnershipKey(route);
            if(state.otherFiles.count(lowerValue(key)))return false;
            target=packageDestination(route,state.game,config::activeGame);logical=route.logical;
            if(target.empty())return false;
            state.paths.push_back({key,target});return true;
        }
        const auto layout=packageRoute(name,state.game.languageFolder);
        if(layout.area==PackageArea::Sd){
            const auto key="sd:"+layout.relative;
            if(state.otherFiles.count(lowerValue(key)))return false;
            if(!coreMapper(name,target,logical,const_cast<GameInstall*>(&state.game)))return false;
            state.paths.push_back({key,target});return true;
        }
        std::string relative;
        if(!mapPackRelativePath(name,state.game.languageFolder,relative))return false;
        if(state.otherFiles.count(lowerValue(relative)))return false;
        const auto path="update/content"+(relative.empty()?"":"/"+relative);
        if(!coreMapper(path,target,logical,const_cast<GameInstall*>(&state.game)))return false;
        state.paths.push_back({relative,target});
        return true;
    };
    const auto marker=install.update.root+"/"+config::VersionRelativePath;
    const auto receipt=mapReceiptPath(stateRoot,id,install.languageFolder,device);
    const auto ownedPath=stateRoot+"/"+id+"~"+install.languageFolder+ownershipSuffix;
    return extractArchive(archive,mapper,&context,error,progress,
        [&](std::string& failure){
            std::string previous;
            if(!readTextFile(marker,previous,65536)){failure="Could not read update.txt. Core must finish first.";return false;}
            std::string owned;
            // Patches retain ownership of unchanged files.
            if(!readInstallState(ownedPath,owned,failure))return false;
            std::set<std::string> unique;
            std::istringstream lines(owned);std::string path;
            while(std::getline(lines,path))if(!path.empty())unique.insert(path);
            for(const auto& [relative,target]:context.paths){
                struct stat st{};
                if(stat(target.c_str(),&st)==0&&S_ISREG(st.st_mode))unique.insert(relative);
            }
            owned.clear();
            for(const auto& value:unique)owned+=value+"\n";
            if(!writeTextFileAtomic(ownedPath,owned,failure))return false;
            if(!writeTextFileAtomic(receipt,version+"\n"+install.update.root+"\n",failure))return false;
            return writeTextFileAtomic(marker,markerWithPack(previous,id,version,install.languageFolder,device),failure);
        },[](std::string&){return true;},beforeMutation,&install);
}

#include "local_package_platform.inc"

bool installTesterArchive(const GameInstall& install, std::string& error,
                          const ProgressCallback& progress) {
    struct stat info{};
    if (lstat(config::TesterArchivePath, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || static_cast<unsigned long long>(info.st_size) > config::MaxArchiveBytes) {
        error = "sd:/unbound/tester.zip must be a non-empty ZIP within HQ's package size limit.";
        return false;
    }
    // Local beta payloads deliberately have no feed/hash requirement. Keep the
    // normal mapper, archive preflight, CRC, free-space and destination checks.
    // Invalidate the public version before the first write, including failures.
    const auto marker = install.update.root + "/" + config::VersionRelativePath;
    return installCoreArchive(config::TesterArchivePath, install, "0.0.0", error, progress,
        [&marker](std::string& markerError) {
            return writeTextFileAtomic(marker, "0.0.0\n", markerError);
        });
}

#ifdef UNBOUND_DIAGNOSTIC_SD
namespace {

bool diagnosticPublishedRepairClear(std::string& error) {
    // Never use config::SdRoot here: the diagnostic app deliberately has a
    // separate SD app directory. A published install may still need repair.
    for (const char* root : {config::PublishedSdRoot, config::LegacyHqSdRoot}) {
        for (const char* suffix : {"", ".unbound-new", ".unbound-old"}) {
            const std::string path = std::string(root) + "/state/core-install.txt" + suffix;
            struct stat state{};
            if (stat(path.c_str(), &state) == 0) {
                error = "The regular HQ has an install repair record. Finish its saved "
                    "installation before testing a local ZIP. This diagnostic will not "
                    "clear or overwrite that record.\n\n" + path;
                return false;
            }
            if (errno != ENOENT) {
                error = "Could not safely check the regular HQ install repair record: " + path;
                return false;
            }
        }
    }
    return true;
}

} // namespace

const char* diagnosticArchivePath() {
    return "fs:/vol/external01/unbound/testing/update.zip";
}

const char* diagnosticInstalledVersion() {
    // Numeric so every published HQ's repair parser accepts it, and deliberately
    // lower than public content versions so a return to regular HQ offers a
    // full restore. This local ZIP is never claimed as a published release.
    return "0.0.0";
}

bool inspectDiagnosticArchive(DiagnosticArchiveInfo& info, std::string& error,
                              const ProgressCallback& progress) {
    info = {};
    error.clear();
    return runBackgroundOperation("HQ diagnostic ZIP", progress,
        [&](const ProgressCallback& relay) {
            struct stat before{};
            if (stat(diagnosticArchivePath(), &before) != 0) {
                if (errno == ENOENT) return true;
                error = "Could not inspect sd:/unbound/testing/update.zip: " +
                        std::string(std::strerror(errno));
                return false;
            }
            info.present = true;
            if (!S_ISREG(before.st_mode) || before.st_size <= 0 ||
                static_cast<unsigned long long>(before.st_size) > config::MaxArchiveBytes) {
                error = "The local update.zip must be a non-empty regular ZIP file no larger than " +
                        std::to_string(config::MaxArchiveBytes) + " bytes";
                return false;
            }
            info.compressedBytes = static_cast<unsigned long long>(before.st_size);
            if (relay) relay("Checking local test ZIP", "Inspecting update.zip...", 0);
            int zipError = 0;
            zip_t* zip = zip_open(diagnosticArchivePath(), ZIP_RDONLY, &zipError);
            if (!zip) { error = "sd:/unbound/testing/update.zip is not a readable ZIP"; return false; }
            const auto count = zip_get_num_entries(zip, ZIP_FL_UNCHANGED);
            const bool zipClosed = zip_close(zip) == 0;
            if (!zipClosed) zip_discard(zip);
            if (count <= 0 || static_cast<unsigned long long>(count) > config::MaxArchiveFiles || !zipClosed) {
                error = "The local ZIP is empty, unreadable, or contains too many entries";
                return false;
            }
            AlignedIoBuffer buffer;
            if (!buffer) { error = "Could not allocate the local ZIP verification buffer"; return false; }
            FILE* input = std::fopen(diagnosticArchivePath(), "rb");
            if (!input) { error = "Could not open the local ZIP for verification"; return false; }
            Sha256 hash;
            unsigned long long readBytes = 0;
            std::uint32_t lastProgress = SDL_GetTicks();
            bool ok = true;
            for (;;) {
                const auto bytes = std::fread(buffer.data(), 1, buffer.size(), input);
                if (!bytes) break;
                if (readBytes > info.compressedBytes || bytes > info.compressedBytes - readBytes) {
                    ok = false;
                    break;
                }
                hash.update(buffer.data(), bytes);
                readBytes += bytes;
                const auto now = SDL_GetTicks();
                if (relay && now - lastProgress >= 150) {
                    relay("Checking local test ZIP", "Verifying " + dataAmount(readBytes) +
                          " of " + dataAmount(info.compressedBytes),
                          static_cast<int>((readBytes * 100ULL) / info.compressedBytes));
                    lastProgress = now;
                }
            }
            if (std::ferror(input)) ok = false;
            if (std::fclose(input) != 0) ok = false;
            struct stat after{};
            if (!ok || readBytes != info.compressedBytes ||
                stat(diagnosticArchivePath(), &after) != 0 || !S_ISREG(after.st_mode) ||
                after.st_size != before.st_size || after.st_mtime != before.st_mtime) {
                error = "The local ZIP could not be read completely or changed during verification. "
                        "Finish copying update.zip to SD and restart the diagnostic app.";
                return false;
            }
            info.sha256 = hash.finish();
            if (relay) relay("Local test ZIP ready", "update.zip verified; no game files have been changed.", 100);
            return true;
        });
}

bool installDiagnosticArchive(const DiagnosticArchiveInfo& info,
                              const GameInstall& install, std::string& error,
                              const ProgressCallback& progress) {
    error.clear();
    if (!info.present || !info.compressedBytes || info.sha256.size() != 64) {
        error = "Inspect the local update.zip before installing it";
        return false;
    }
    if (!diagnosticPublishedRepairClear(error) ||
        !revalidateInstallDestination(install, error, true)) return false;
    DiagnosticArchiveInfo current;
    if (!inspectDiagnosticArchive(current, error, progress)) return false;
    if (!current.present || current.compressedBytes != info.compressedBytes ||
        current.sha256 != info.sha256) {
        error = "update.zip changed after confirmation. No game files were changed. "
                "Restart the diagnostic app to inspect and confirm the new ZIP.";
        return false;
    }
    const std::string marker = install.update.root + "/" + config::VersionRelativePath;
    const InstallCommit markLocalTest = [&](std::string& markerError) {
        if (!diagnosticPublishedRepairClear(markerError) ||
            !revalidateInstallDestination(install, markerError, true)) return false;
        return writeTextFileAtomic(marker, std::string(diagnosticInstalledVersion()) + "\n", markerError);
    };
    // Identical mapper, preflight, space checks, CRC validation, worker/progress
    // and per-file replacement used by installCoreArchive. Only the marker is
    // different: invalidate a published claim before the first local write,
    // then commit the same explicit test marker once every payload succeeds.
    return extractArchive(diagnosticArchivePath(), coreMapper,
                          const_cast<GameInstall*>(&install), error, progress,
                          [&marker](std::string& commitError) {
                              return writeTextFileAtomic(marker,
                                  std::string(diagnosticInstalledVersion()) + "\n", commitError);
                          }, markLocalTest);
}
#endif

bool installWorkshopArchive(const std::string& archive, const GameInstall& install,
                            const WorkshopMod& mod, std::string& error,
                            const ProgressCallback& progress) {
    if (!install.update.present || !install.destinationReady) {
        error = "Choose a valid install destination before installing a workshop mod.";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    WorkshopContext context{&install, &mod};
    return extractArchive(archive, workshopMapper, &context, error, progress);
}

bool scanLocalMods(const GameInstall& install, bool installed, std::vector<LocalMod>& mods,
                   std::string& error, bool allowRecoveryWrites) {
    mods.clear();
    if (installed && !install.destinationReady) allowRecoveryWrites = false;
    if (installed && !install.update.present) {
        error = "The game update is not installed or mounted";
        return false;
    }
    const std::string root = installed ? install.update.root + "/content/mods"
                                       : config::gameSdModsRoot();
    if (installed) {
        if (!exists(root)) return true;
        struct stat rootInfo{};
        if (stat(root.c_str(), &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode)) {
            error = "The game's content/mods path is not a readable folder";
            return false;
        }
    } else {
        if (!exists(root)) {
            if (!allowRecoveryWrites) return true;
            if (!ensureDirectory(root)) {
                error = "Could not create sd:/unbound/content/mods";
                return false;
            }
        }
        struct stat rootInfo{};
        if (stat(root.c_str(), &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode)) {
            error = "sd:/unbound/content/mods is not a readable folder";
            return false;
        }
    }
    if (allowRecoveryWrites && !reconcileInterruptedModDeletes(root, error))
        return false;
    DIR* directory = opendir(root.c_str());
    if (!directory) { error = "Could not read " + root; return false; }
    std::unordered_set<std::string> foldedFolders;
    while (dirent* entry = readdir(directory)) {
        const std::string folder(entry->d_name);
        if (folder == "." || folder == ".." || !validModFolder(folder)) continue;
        const std::string path = root + "/" + folder;
        struct stat info{};
        if (stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) continue;
        std::string manifest;
        if (!readTextFile(path + "/modload.txt", manifest, 16 * 1024)) continue;
        if (!foldedFolders.insert(caseFoldedModFolder(folder)).second) {
            closedir(directory);
            mods.clear();
            error = "Two mod folders differ only by letter case. Rename one before continuing.";
            return false;
        }
        mods.push_back(parseModload(manifest, folder, installed));
    }
    closedir(directory);
    std::sort(mods.begin(), mods.end(), [](const LocalMod& a, const LocalMod& b) {
        return a.name < b.name;
    });
    return true;
}

bool scanLocalModInventory(const GameInstall& install,
                           std::vector<LocalModInventoryItem>& inventory,
                           std::string& error, bool allowRecoveryWrites) {
    inventory.clear();
    error.clear();

    std::vector<LocalMod> sdMods;
    if (!scanLocalMods(install, false, sdMods, error, allowRecoveryWrites))
        return false;
    std::vector<LocalMod> consoleMods;
    if (install.update.present &&
        !scanLocalMods(install, true, consoleMods, error, allowRecoveryWrites))
        return false;

    std::map<std::string, LocalModInventoryItem> grouped;
    for (const auto& mod : sdMods) {
        const std::string key = caseFoldedModFolder(mod.folder);
        auto& item = grouped[key];
        item.folder = mod.folder;
        item.sd = mod;
        item.hasSd = true;
    }
    for (const auto& mod : consoleMods) {
        const std::string key = caseFoldedModFolder(mod.folder);
        auto& item = grouped[key];
        if (item.folder.empty()) item.folder = mod.folder;
        item.console = mod;
        item.hasConsole = true;
    }
    inventory.reserve(grouped.size());
    for (auto& [key, item] : grouped) {
        (void)key;
        inventory.push_back(std::move(item));
    }
    std::sort(inventory.begin(), inventory.end(), [](const auto& a, const auto& b) {
        const LocalMod& aDisplay = a.hasSd ? a.sd : a.console;
        const LocalMod& bDisplay = b.hasSd ? b.sd : b.console;
        const std::string aName = caseFoldedModFolder(aDisplay.name);
        const std::string bName = caseFoldedModFolder(bDisplay.name);
        if (aName != bName) return aName < bName;
        return caseFoldedModFolder(a.folder) < caseFoldedModFolder(b.folder);
    });
    return true;
}

bool inspectLegacySdMods(LegacyModMigrationInfo& info, std::string& error) {
    info = {};
    error.clear();
    static constexpr const char* LegacyRoots[] = {
        "fs:/vol/external01/unbound/mods",
        "fs:/vol/external01/unbound/mod",
    };
    for (const char* root : LegacyRoots) {
        struct stat rootInfo{};
        if (stat(root, &rootInfo) != 0) {
            if (errno == ENOENT) continue;
            error = std::string("Could not inspect legacy mod path ") + root;
            return false;
        }
        if (!S_ISDIR(rootInfo.st_mode)) {
            error = std::string("The legacy mod path is not a folder: ") + root;
            return false;
        }
        info.roots.emplace_back(root);
        DIR* directory = opendir(root);
        if (!directory) {
            error = std::string("Could not read legacy mod path ") + root;
            return false;
        }
        while (dirent* entry = readdir(directory)) {
            const std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            const std::string path = std::string(root) + "/" + name;
            struct stat entryInfo{};
            if (stat(path.c_str(), &entryInfo) == 0 && S_ISDIR(entryInfo.st_mode) &&
                validModFolder(name)) {
                ++info.folderCount;
                info.folders.push_back(name);
            } else {
                ++info.otherEntryCount;
            }
        }
        closedir(directory);
    }
    return true;
}

bool migrateLegacySdMods(LegacyModMigrationResult& result, std::string& error) {
    result = {};
    error.clear();
    if (!ensureDirectory(config::gameSdModsRoot())) {
        error = "Could not create sd:/unbound/content/mods";
        return false;
    }
    if (!reconcileInterruptedModDeletes(config::gameSdModsRoot(), error)) return false;

    static constexpr const char* LegacyRoots[] = {
        "fs:/vol/external01/unbound/mods",
        "fs:/vol/external01/unbound/mod",
    };
    for (const char* root : LegacyRoots) {
        struct stat rootInfo{};
        if (stat(root, &rootInfo) != 0) {
            if (errno == ENOENT) continue;
            error = std::string("Could not inspect legacy mod path ") + root;
            return false;
        }
        if (!S_ISDIR(rootInfo.st_mode)) {
            error = std::string("The legacy mod path is not a folder: ") + root;
            return false;
        }
        DIR* directory = opendir(root);
        if (!directory) {
            error = std::string("Could not read legacy mod path ") + root;
            return false;
        }
        std::vector<std::string> entries;
        while (dirent* entry = readdir(directory)) {
            const std::string name(entry->d_name);
            if (name != "." && name != "..") entries.push_back(name);
        }
        closedir(directory);
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return caseFoldedModFolder(a) < caseFoldedModFolder(b);
        });

        for (const auto& name : entries) {
            const std::string source = std::string(root) + "/" + name;
            struct stat sourceInfo{};
            if (stat(source.c_str(), &sourceInfo) != 0 || !S_ISDIR(sourceInfo.st_mode) ||
                !validModFolder(name)) {
                ++result.skipped;
                continue;
            }
            bool collision = false;
            std::string collisionName;
            if (!findCaseInsensitiveChild(config::gameSdModsRoot(), name, collision,
                                          collisionName, error))
                return false;
            if (collision) {
                ++result.conflicts;
                result.conflictFolders.push_back(name);
                continue;
            }
            const std::string destination = std::string(config::gameSdModsRoot()) + "/" + name;
            if (std::rename(source.c_str(), destination.c_str()) != 0) {
                error = "Could not move legacy mod folder " + name +
                        " into sd:/unbound/content/mods";
                return false;
            }
            ++result.moved;
        }

        // Only remove a legacy root when it is genuinely empty. Loose files,
        // unsafe names and collisions are intentionally left for the user.
        (void)rmdir(root);
        if (exists(root)) result.rootsStillPresent.emplace_back(root);
    }
    return true;
}

bool loadModLinkSetting(bool& enabled, std::string& warning,
                        bool allowRecoveryWrites) {
    enabled = false;
    warning.clear();
    if (allowRecoveryWrites &&
        !recoverAtomicTextFile(config::ModLinkDisableFlagPath)) {
        warning = "The Unbound Mod Link switch has an interrupted SD-card update. "
                  "Mod Link was kept OFF until the flag can be recovered.";
        return false;
    }
    struct stat info{};
    if (stat(config::ModLinkDisableFlagPath, &info) == 0) {
        if (!S_ISREG(info.st_mode)) {
            warning = "The Unbound Mod Link disable flag is not a regular file. "
                      "Mod Link was kept OFF.";
            return false;
        }
        return true;
    }
    if (errno != ENOENT) {
        warning = "The Unbound Mod Link setting could not be read. Mod Link was kept OFF.";
        return false;
    }
    enabled = true;
    return true;
}

bool saveModLinkSetting(bool enabled, std::string& error) {
    error.clear();
    if (!ensureDirectory(config::gameSdModsRoot())) {
        error = "Could not create sd:/unbound/content/mods";
        return false;
    }
    if (!recoverAtomicTextFile(config::ModLinkDisableFlagPath)) {
        error = "Could not recover the Unbound Mod Link setting";
        return false;
    }
    if (!enabled)
        return writeTextFileAtomic(config::ModLinkDisableFlagPath,
                                   "Unbound Mod Link disabled by HQ\n", error);
    if (std::remove(config::ModLinkDisableFlagPath) != 0 && errno != ENOENT) {
        error = "Could not enable Unbound Mod Link by removing its disable flag";
        return false;
    }
    return true;
}

bool deleteSdMod(const LocalMod& mod, std::string& error,
                 const ProgressCallback& progress) {
    error.clear();
    if (mod.installed || !validModFolder(mod.folder)) {
        error = "Invalid SD mod selection";
        return false;
    }
    const std::string root = config::gameSdModsRoot();
    if (!reconcileInterruptedModDeletes(root, error)) return false;
    const std::string target = root + "/" + mod.folder;
    const std::string pendingDelete = target + ".unbound-delete";
    struct stat info{};
    if (stat(target.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        error = "The selected SD mod folder no longer exists";
        return false;
    }
    if (progress) progress("Removing " + mod.name,
                           "Preserving the folder for safe deletion...", 20);
    if (std::rename(target.c_str(), pendingDelete.c_str()) != 0) {
        error = "Could not prepare the selected SD mod for deletion";
        return false;
    }
    if (progress) progress("Removing " + mod.name,
                           "Deleting the selected SD mod...", 55);
    if (!removeTree(pendingDelete)) {
        error = "The mod was removed from the active list, but " + pendingDelete +
                " could not be completely deleted. Inspect that folder before retrying.";
        return false;
    }
    if (progress) progress("Removing " + mod.name,
                           "The SD mod was removed successfully.", 100);
    return true;
}

bool loadSdAutoSyncSetting(bool& enabled, std::string& warning,
                           bool allowRecoveryWrites) {
    enabled = false;
    warning.clear();
    if (allowRecoveryWrites) {
        if (!recoverAtomicTextFile(config::SettingsPath)) {
            warning = "An interrupted settings.ini update could not be recovered. "
                      "SD Auto-Sync was kept OFF.";
            return false;
        }
    } else if (exists(std::string(config::SettingsPath) + ".unbound-new") ||
               exists(std::string(config::SettingsPath) + ".unbound-old")) {
        warning = "settings.ini needs recovery, but HQ is read-only this session. SD Auto-Sync was kept OFF.";
        return false;
    }
    if (!exists(config::SettingsPath)) return true;
    std::string text;
    if (!readTextFile(config::SettingsPath, text, config::MaxSettingsBytes)) {
        warning = "settings.ini could not be read. SD Auto-Sync was kept OFF.";
        return false;
    }
    bool found = false;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' ||
            (line.front() == '[' && line.back() == ']'))
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = trim(line.substr(0, equals));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (key != "sd_auto_sync") continue;
        if (found) {
            warning = "settings.ini contains more than one sd_auto_sync value. "
                      "SD Auto-Sync was kept OFF.";
            enabled = false;
            return false;
        }
        found = true;
        std::string value = trim(line.substr(equals + 1));
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (value == "on" || value == "true" || value == "1") enabled = true;
        else if (value == "off" || value == "false" || value == "0") enabled = false;
        else {
            warning = "settings.ini has an invalid sd_auto_sync value on line " +
                      std::to_string(lineNumber) + ". SD Auto-Sync was kept OFF.";
            enabled = false;
            return false;
        }
    }
    return true;
}

bool saveSdAutoSyncSetting(bool enabled, std::string& error) {
    std::vector<std::string> retained;
    if (!recoverAtomicTextFile(config::SettingsPath)) {
        error = "Could not recover an interrupted settings.ini update";
        return false;
    }
    if (exists(config::SettingsPath)) {
        std::string existing;
        if (!readTextFile(config::SettingsPath, existing, config::MaxSettingsBytes)) {
            error = "Could not read settings.ini before updating it";
            return false;
        }
        std::istringstream stream(existing);
        std::string line;
        while (std::getline(stream, line)) {
            const std::string stripped = trim(line);
            const auto equals = stripped.find('=');
            std::string key = equals == std::string::npos
                ? std::string{} : trim(stripped.substr(0, equals));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            if (key != "sd_auto_sync") retained.push_back(line);
        }
    } else {
        retained.push_back("# Black Ops 2 Unbound HQ settings");
    }
    std::string output;
    for (const auto& line : retained) output += line + "\n";
    output += std::string("sd_auto_sync=") + (enabled ? "on\n" : "off\n");
    if (output.size() > config::MaxSettingsBytes) {
        error = "settings.ini exceeds its safety limit";
        return false;
    }
    if (!writeTextFileAtomic(config::SettingsPath, output, error)) {
        error = "Could not save the SD Auto-Sync setting: " + error;
        return false;
    }
    return true;
}

bool loadPluginAutoUpdateSetting(const std::string& pluginKey, bool& enabled,
                                 std::string& warning,
                                 bool allowRecoveryWrites) {
    enabled = false;
    warning.clear();
    if (!validId(pluginKey)) {
        warning = "The plugin key is invalid. Auto-update was kept OFF.";
        return false;
    }
    if (allowRecoveryWrites) {
        if (!recoverAtomicTextFile(config::SettingsPath)) {
            warning = "An interrupted settings.ini update could not be recovered. Plugin auto-update was kept OFF.";
            return false;
        }
    } else if (exists(std::string(config::SettingsPath) + ".unbound-new") ||
               exists(std::string(config::SettingsPath) + ".unbound-old")) {
        warning = "settings.ini needs recovery, but HQ is read-only this session. Plugin auto-update was kept OFF.";
        return false;
    }
    if (!exists(config::SettingsPath)) return true;
    std::string text;
    if (!readTextFile(config::SettingsPath, text, config::MaxSettingsBytes)) {
        warning = "settings.ini could not be read. Plugin auto-update was kept OFF.";
        return false;
    }
    const std::string wanted = "plugin_auto_update." + lowerValue(pluginKey);
    bool found = false;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' ||
            (line.front() == '[' && line.back() == ']'))
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = lowerValue(trim(line.substr(0, equals)));
        if (key != wanted) continue;
        if (found) {
            warning = "settings.ini contains more than one " + wanted +
                      " value. Plugin auto-update was kept OFF.";
            enabled = false;
            return false;
        }
        found = true;
        const std::string value = lowerValue(trim(line.substr(equals + 1)));
        if (value == "on" || value == "true" || value == "1") enabled = true;
        else if (value == "off" || value == "false" || value == "0") enabled = false;
        else {
            warning = "settings.ini has an invalid " + wanted + " value on line " +
                      std::to_string(lineNumber) + ". Plugin auto-update was kept OFF.";
            enabled = false;
            return false;
        }
    }
    return true;
}

bool savePluginAutoUpdateSetting(const std::string& pluginKey, bool enabled,
                                 std::string& error) {
    if (!validId(pluginKey)) { error = "The plugin key is invalid"; return false; }
    if (!recoverAtomicTextFile(config::SettingsPath)) {
        error = "Could not recover an interrupted settings.ini update";
        return false;
    }
    const std::string wanted = "plugin_auto_update." + lowerValue(pluginKey);
    std::vector<std::string> retained;
    if (exists(config::SettingsPath)) {
        std::string existing;
        if (!readTextFile(config::SettingsPath, existing, config::MaxSettingsBytes)) {
            error = "Could not read settings.ini before updating it";
            return false;
        }
        std::istringstream stream(existing);
        std::string line;
        while (std::getline(stream, line)) {
            const std::string stripped = trim(line);
            const auto equals = stripped.find('=');
            const std::string key = equals == std::string::npos
                ? std::string{} : lowerValue(trim(stripped.substr(0, equals)));
            if (key != wanted) retained.push_back(line);
        }
    } else {
        retained.push_back("# Black Ops 2 Unbound HQ settings");
    }
    std::string output;
    for (const auto& line : retained) output += line + "\n";
    output += wanted + "=" + (enabled ? "on\n" : "off\n");
    if (output.size() > config::MaxSettingsBytes) {
        error = "settings.ini exceeds its safety limit";
        return false;
    }
    if (!writeTextFileAtomic(config::SettingsPath, output, error)) {
        error = "Could not save the plugin auto-update setting: " + error;
        return false;
    }
    return true;
}

bool inspectPluginInstall(const WorkshopPlugin& plugin, const PluginRelease* latest,
                          PluginInstallStatus& status, std::string& warning,
                          bool allowRecoveryWrites) {
    status = {};
    warning.clear();
    std::string recoveryError;
    if (allowRecoveryWrites) {
        if (!recoverPluginTransaction(plugin, recoveryError)) {
            status.presence = PluginPresence::Damaged;
            warning = recoveryError;
            return false;
        }
    } else {
        const std::string journal = pluginTransactionPath(plugin);
        const std::string preinstall = pluginPreinstallReceiptPath(plugin);
        bool recoveryPending = exists(journal) || exists(journal + ".unbound-new") ||
                               exists(journal + ".unbound-old") || exists(preinstall) ||
                               exists(preinstall + ".unbound-new") ||
                               exists(preinstall + ".unbound-old");
        for (const auto& path : plugin.managedPaths) {
            const std::string target = pluginTargetPath(path);
            recoveryPending = recoveryPending || exists(target + ".unbound-new") ||
                              exists(target + ".unbound-old");
        }
        if (recoveryPending) {
            status.presence = PluginPresence::Damaged;
            warning = "An interrupted plugin transaction needs recovery. HQ is read-only this session, so every file was left untouched.";
            return false;
        }
    }
    PluginRelease receipt;
    bool receiptFound = false;
    std::string receiptError;
    if (!loadPluginReceipt(plugin, receipt, receiptFound, receiptError,
                           allowRecoveryWrites)) {
        status.presence = PluginPresence::Damaged;
        warning = receiptError;
        return false;
    }
    if (!receiptFound) {
        bool any = false, all = false;
        if (!curatedTargetsPresent(plugin, any, all, warning)) {
            status.presence = PluginPresence::Damaged;
            return false;
        }
        status.presence = any ? PluginPresence::Unmanaged : PluginPresence::NotInstalled;
        status.installedVersion = any ? "Unknown" : "Not installed";
        return true;
    }
    status.installedRevision = receipt.revision;
    status.installedVersion = receipt.version;
    std::string integrityError;
    if (!pluginFilesMatch(receipt, integrityError)) {
        status.presence = PluginPresence::Damaged;
        status.repairable = true;
        warning = integrityError;
        return true;
    }
    status.presence = PluginPresence::Managed;
    if (latest) {
        if (latest->revision == receipt.revision && !samePluginRelease(*latest, receipt)) {
            status.presence = PluginPresence::Damaged;
            warning = "Plugin revision " + std::to_string(latest->revision) +
                      " was reused with different release metadata";
            return true;
        }
        status.updateAvailable = latest->revision > receipt.revision;
        if (latest->revision < receipt.revision)
            warning = "The plugin release feed is older than the installed receipt; downgrade was blocked.";
    }
    return true;
}

bool installPluginArchive(const std::string& archive, const WorkshopPlugin& plugin,
                          const PluginRelease& release, bool replaceUnmanaged,
                          std::string& error, const ProgressCallback& progress) {
    if (!recoverPluginTransaction(plugin, error)) return false;
    struct stat archiveInfo{};
    if (stat(archive.c_str(), &archiveInfo) != 0 || !S_ISREG(archiveInfo.st_mode) ||
        static_cast<std::uint64_t>(archiveInfo.st_size) != release.compressedBytes ||
        release.compressedBytes > config::MaxPluginArchiveBytes) {
        error = "The plugin ZIP does not match its declared download size";
        return false;
    }
    std::string archiveHash;
    if (!sha256File(archive, archiveHash) ||
        lowerValue(archiveHash) != lowerValue(release.sha256)) {
        error = "The plugin ZIP failed its SHA-256 check";
        return false;
    }
    unsigned long long expandedBytes = 0;
    if (!validatePluginZip(archive, plugin, release, expandedBytes, error)) return false;

    PluginRelease previousReceipt;
    bool receiptFound = false;
    if (!loadPluginReceipt(plugin, previousReceipt, receiptFound, error)) return false;
    if (receiptFound) {
        if (previousReceipt.revision > release.revision) {
            error = "Plugin downgrade was blocked";
            return false;
        }
        if (previousReceipt.revision == release.revision &&
            !samePluginRelease(previousReceipt, release)) {
            error = "Plugin revision " + std::to_string(release.revision) +
                    " was reused with different metadata";
            return false;
        }
    } else {
        bool any = false, all = false;
        if (!curatedTargetsPresent(plugin, any, all, error)) return false;
        if (any && !replaceUnmanaged) {
            error = "Existing unmanaged Aroma files were found. Use Replace & Manage from the plugin page to preserve and replace them explicitly.";
            return false;
        }
    }

    unsigned long long existingBytes = 0;
    std::unordered_map<std::string, bool> existedBefore;
    for (const auto& path : plugin.managedPaths) {
        const std::string target = pluginTargetPath(path);
        struct stat info{};
        const bool present = stat(target.c_str(), &info) == 0;
        if (present && !S_ISREG(info.st_mode)) {
            error = "A plugin destination is not a regular file: " + path;
            return false;
        }
        if (present) {
            const auto size = static_cast<unsigned long long>(info.st_size);
            if (existingBytes > config::MaxPluginExpandedBytes ||
                size > config::MaxPluginExpandedBytes - existingBytes) {
                error = "Existing plugin files exceed the safety limit";
                return false;
            }
            existingBytes += size;
        }
        existedBefore.emplace(lowerValue(path), present);
        if (exists(target + ".unbound-new") || exists(target + ".unbound-old")) {
            error = "Unowned recovery files were found beside " + path +
                    ". HQ left them untouched.";
            return false;
        }
    }
    if (expandedBytes > std::numeric_limits<unsigned long long>::max() - existingBytes ||
        !hasSpaceFor(config::SdRoot, expandedBytes + existingBytes, error))
        return false;

    const std::string receiptPath = pluginReceiptPath(plugin);
    const std::string preinstall = pluginPreinstallReceiptPath(plugin);
    if (receiptFound) {
        std::string previousText;
        if (!readTextFile(receiptPath, previousText, config::MaxManifestBytes) ||
            !writeTextFileAtomic(preinstall, previousText, error)) {
            error = "Could not preserve the prior plugin receipt: " + error;
            return false;
        }
    }
    std::string journalText = "# UNBOUND_PLUGIN_TRANSACTION_V1\nREVISION|" +
                              std::to_string(release.revision) + "\n";
    for (const auto& path : plugin.managedPaths)
        journalText += "FILE|" + path + "|" +
                       (existedBefore[lowerValue(path)] ? "1\n" : "0\n");
    const std::string journal = pluginTransactionPath(plugin);
    if (!writeTextFileAtomic(journal, journalText, error)) {
        if (exists(preinstall)) std::remove(preinstall.c_str());
        error = "Could not begin the plugin transaction: " + error;
        return false;
    }

    auto failAndRecover = [&](const std::string& reason) {
        std::string recoveryError;
        if (!recoverPluginTransaction(plugin, recoveryError))
            error = reason + "\nAutomatic rollback also failed: " + recoveryError;
        else error = reason;
        return false;
    };

    FileIoContext io;
    if (!io.ready()) return failAndRecover("Could not allocate plugin ZIP transfer buffers");
    int zipError = 0;
    zip_t* zip = zip_open(archive.c_str(), ZIP_RDONLY, &zipError);
    if (!zip) return failAndRecover("Could not reopen the verified plugin ZIP");
    InstallProgress installProgress{&progress, expandedBytes, 0, {}};
    installProgress.report("Preparing Aroma plugin files...");
    for (const auto& file : release.files) {
        const zip_int64_t index = zip_name_locate(zip, file.archivePath.c_str(), 0);
        if (index < 0) {
            zip_close(zip);
            return failAndRecover("The verified plugin ZIP changed while being installed");
        }
        const std::string target = pluginTargetPath(file.archivePath);
        if (!io.prepareDirectory(parentPath(target))) {
            zip_close(zip);
            return failAndRecover(io.directoryError());
        }
        const std::string temporary = target + ".unbound-new";
        FILE* output = std::fopen(temporary.c_str(), "wb");
        if (!output) {
            zip_close(zip);
            return failAndRecover("Could not stage " + file.archivePath);
        }
        if (std::setvbuf(output, io.output.data(), _IOFBF, io.output.size()) != 0) {
            std::fclose(output);
            zip_close(zip);
            return failAndRecover("Could not buffer " + file.archivePath);
        }
        zip_file_t* entry = zip_fopen_index(zip, static_cast<zip_uint64_t>(index),
                                            ZIP_FL_UNCHANGED);
        if (!entry) {
            std::fclose(output);
            zip_close(zip);
            return failAndRecover("Could not open " + file.archivePath + " in the ZIP");
        }
        Sha256 writtenHash;
        bool ok = true;
        unsigned long long written = 0;
        const std::string phase = "Unpacking " + leafName(file.archivePath) + "...";
        for (;;) {
            const zip_int64_t count = zip_fread(entry, io.transfer.data(), io.transfer.size());
            if (count < 0) { ok = false; break; }
            if (!count) break;
            const auto chunkBytes = static_cast<std::size_t>(count);
            if (written > file.bytes || chunkBytes > file.bytes - written ||
                std::fwrite(io.transfer.data(), 1, chunkBytes, output) != chunkBytes) {
                ok = false;
                break;
            }
            writtenHash.update(io.transfer.data(), chunkBytes);
            written += chunkBytes;
            installProgress.report(phase, static_cast<unsigned long long>(count));
        }
        if (zip_fclose(entry) != 0) ok = false;
        if (ok && std::fflush(output) != 0) ok = false;
        if (ok && fsync(fileno(output)) != 0) ok = false;
        if (std::fclose(output) != 0) ok = false;
        if (!ok || written != file.bytes ||
            lowerValue(writtenHash.finish()) != lowerValue(file.sha256)) {
            zip_close(zip);
            return failAndRecover("Could not safely stage " + file.archivePath);
        }
    }
    installProgress.finishing("Activating the verified Aroma plugin files...");
    if (zip_close(zip) != 0)
        return failAndRecover("Could not close the plugin ZIP cleanly");

    for (const auto& path : plugin.managedPaths) {
        if (!existedBefore[lowerValue(path)]) continue;
        const std::string target = pluginTargetPath(path);
        if (std::rename(target.c_str(), (target + ".unbound-old").c_str()) != 0)
            return failAndRecover("Could not preserve the installed copy of " + path);
    }
    for (const auto& path : plugin.managedPaths) {
        const std::string target = pluginTargetPath(path);
        if (std::rename((target + ".unbound-new").c_str(), target.c_str()) != 0)
            return failAndRecover("Could not commit " + path);
    }
    if (!writeTextFileAtomic(receiptPath, serializePluginRelease(release), error))
        return failAndRecover("Could not commit the plugin installation receipt: " + error);

    // Once the receipt and both files agree, the transaction is complete. If
    // cleanup is interrupted, the journal lets the next run finish safely.
    if (!cleanupPluginArtifacts(plugin) ||
        (std::remove(journal.c_str()) != 0 && exists(journal))) {
        error = "The verified plugin files and receipt were committed, but HQ could not remove all transaction recovery files. The package was retained; relaunch HQ to retry recovery before using the plugin.";
        installProgress.finish();
        return false;
    }
    installProgress.finish();
    return true;
}

bool synchronizeSdMods(const GameInstall& install, SdModSyncResult& result,
                       std::string& error, const ProgressCallback& progress) {
    result = {};
    if (!install.update.present || !install.destinationReady) {
        error = "The official game update is not installed or mounted";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    if (progress)
        progress("SD Auto-Sync", "Scanning sd:/unbound/content/mods for manifested mod folders...", 0);
    std::vector<LocalMod> mods;
    if (!scanLocalMods(install, false, mods, error)) return false;
    if (mods.size() > config::MaxArchiveFiles) {
        error = "Too many SD mod folders were found for one safe Auto-Sync run";
        return false;
    }
    ModSyncLedger ledger;
    if (!loadModSyncLedger(install, ledger, error)) return false;
    const std::string modsRoot = install.update.root + "/content/mods";
    if (!reconcileInterruptedModDeletes(modsRoot, error)) return false;
    // A pre-existing reserved copy artifact has no durable transaction proof.
    // Leave it untouched and fail closed rather than guessing who created it.
    for (const auto& [folder, token] : ledger) {
        (void)token;
        if (!recoverInterruptedModCopy(modsRoot + "/" + folder, error)) return false;
    }
    for (const auto& mod : mods) {
        bool targetExists = false;
        std::string targetFolder;
        if (!findCaseInsensitiveChild(modsRoot, mod.folder, targetExists,
                                      targetFolder, error))
            return false;
        if (!recoverInterruptedModCopy(modsRoot + "/" +
                                       (targetExists ? targetFolder : mod.folder), error))
            return false;
    }
    std::unordered_set<std::string> sourceFolders;
    for (const auto& mod : mods)
        sourceFolders.insert(caseFoldedModFolder(mod.folder));
    bool prunedOwnership = false;
    for (auto entry = ledger.begin(); entry != ledger.end();) {
        if (sourceFolders.find(caseFoldedModFolder(entry->first)) == sourceFolders.end()) {
            entry = ledger.erase(entry);
            prunedOwnership = true;
        } else {
            ++entry;
        }
    }
    // Removing an SD source relinquishes ownership but deliberately leaves the
    // installed target alone. Persist that before any copy so a later unrelated
    // same-name target can never be silently treated as HQ-managed.
    if (prunedOwnership && !saveModSyncLedger(install, ledger, error)) return false;

    struct PendingMod {
        LocalMod mod;
        std::string targetFolder;
        std::string ownedLedgerFolder;
        bool replacing = false;
    };
    std::vector<PendingMod> pending;
    bool relinquishedOwnership = false;
    for (const auto& mod : mods) {
        bool targetExists = false;
        std::string targetFolder;
        if (!findCaseInsensitiveChild(modsRoot, mod.folder, targetExists,
                                      targetFolder, error))
            return false;
        if (!targetExists) targetFolder = mod.folder;
        const std::string target = modsRoot + "/" + targetFolder;
        if (targetExists) {
            struct stat targetInfo{};
            if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode)) {
                ++result.conflicts;
                result.conflictFolders.push_back(mod.folder);
                continue;
            }
        }
        auto owned = findLedgerFolder(ledger, targetFolder);
        if (targetExists && owned == ledger.end()) {
            ++result.conflicts;
            result.conflictFolders.push_back(mod.folder);
            continue;
        }
        if (targetExists && owned != ledger.end() && !owned->second.empty()) {
            if (!installedFolderMatchesOwnership(target, owned->second)) {
                ledger.erase(owned);
                relinquishedOwnership = true;
                ++result.conflicts;
                result.conflictFolders.push_back(mod.folder);
                continue;
            }
            owned = findLedgerFolder(ledger, targetFolder);
        }
        if (targetExists && !mod.syncToken.empty() && owned != ledger.end() &&
            owned->second == mod.syncToken) {
            ++result.unchanged;
            continue;
        }
        pending.push_back({mod, targetFolder,
                           owned == ledger.end() ? std::string{} : owned->first,
                           targetExists});
    }
    if (relinquishedOwnership && !saveModSyncLedger(install, ledger, error)) return false;

    for (std::size_t index = 0; index < pending.size(); ++index) {
        auto& item = pending[index];
        const std::string source = std::string(config::gameSdModsRoot()) + "/" + item.mod.folder;
        const std::string target = modsRoot + "/" + item.targetFolder;
        std::string manifest;
        if (!readTextFile(source + "/modload.txt", manifest, 16 * 1024)) {
            error = "Could not reread " + item.mod.folder + "/modload.txt";
            return false;
        }
        item.mod.syncToken = modSyncTokenFromManifest(manifest);
        unsigned files = 0;
        unsigned long long bytes = 0;
        if (!countTreeFiles(source, 0, files, bytes) || files == 0) {
            error = item.mod.folder + " is empty, unreadable, or exceeds safety limits";
            return false;
        }
        ModSyncLedger updated = ledger;
        if (!item.ownedLedgerFolder.empty()) updated.erase(item.ownedLedgerFolder);
        updated[item.targetFolder] = item.mod.syncToken;
        const auto syncProgress = [&, index](const std::string&, const std::string& detail,
                                             int modPercent) {
            if (!progress) return;
            const int bounded = std::clamp(modPercent, 0, 100);
            const int total = pending.empty() ? 100 :
                static_cast<int>((index * 100ULL + bounded) / pending.size());
            progress("SD Auto-Sync",
                     "Copying " + item.mod.name + " (" + std::to_string(index + 1) +
                         "/" + std::to_string(pending.size()) + ")\n" + detail,
                     total);
        };
        const auto commit = [&](std::string& commitError) {
            std::string installedManifest;
            if (!readTextFile(target + "/modload.txt", installedManifest, 16 * 1024) ||
                modSyncTokenFromManifest(installedManifest) != item.mod.syncToken) {
                commitError = "The mod manifest changed while it was being copied";
                return false;
            }
            return saveModSyncLedger(install, updated, commitError);
        };
        if (!copyModFolderTransactional(source, target, bytes, syncProgress,
                                        commit, error)) {
            error = "Auto-Sync could not install " + item.mod.folder + ": " + error;
            if (result.copied || result.updated)
                error += "\n\nFolders completed before this error remain installed and owned. "
                         "The next launch will resume safely. Completed this run: " +
                         std::to_string(result.copied + result.updated) + ".";
            return false;
        }
        ledger = std::move(updated);
        if (item.replacing) ++result.updated;
        else ++result.copied;
    }
    if (progress) {
        progress("SD Auto-Sync",
                 pending.empty() ? "No SD mod files needed to be copied."
                                 : "All selected SD mod folders were copied successfully.",
                 100);
    }
    return true;
}

bool sdModRequiresOwnershipAdoption(const GameInstall& install, const LocalMod& mod,
                                    bool& required, std::string& error) {
    required = false;
    if (!install.update.present || !install.destinationReady) {
        error = "The game update is not available";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    if (mod.installed || !validModFolder(mod.folder)) {
        error = "Invalid SD mod selection";
        return false;
    }
    ModSyncLedger ledger;
    if (!loadModSyncLedger(install, ledger, error)) return false;
    const std::string modsRoot = install.update.root + "/content/mods";
    bool targetExists = false;
    std::string targetFolder;
    if (!findCaseInsensitiveChild(modsRoot, mod.folder, targetExists,
                                  targetFolder, error))
        return false;
    if (!targetExists) return true;
    const std::string target = modsRoot + "/" + targetFolder;
    struct stat targetInfo{};
    if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode)) {
        error = "An entry named " + targetFolder +
                " already exists in the game's mods folder and is not a folder";
        return false;
    }
    const auto owned = findLedgerFolder(ledger, targetFolder);
    if (!recoverInterruptedModCopy(target, error)) return false;
    required = owned == ledger.end() ||
        !installedFolderMatchesOwnership(target, owned->second);
    return true;
}

bool copySdModToGame(const GameInstall& install, const LocalMod& mod, std::string& error,
                     bool adoptUnmanagedFolder, const ProgressCallback& progress) {
    if (!install.update.present || !install.destinationReady) {
        error = "Choose a valid install destination before copying a mod to the console.";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    if (mod.installed || !validModFolder(mod.folder)) { error = "Invalid SD mod selection"; return false; }
    const std::string source = std::string(config::gameSdModsRoot()) + "/" + mod.folder;
    const std::string modsRoot = install.update.root + "/content/mods";
    bool targetExists = false;
    std::string targetFolder;
    if (!findCaseInsensitiveChild(modsRoot, mod.folder, targetExists,
                                  targetFolder, error))
        return false;
    if (!targetExists) targetFolder = mod.folder;
    const std::string target = modsRoot + "/" + targetFolder;
    if (targetExists) {
        struct stat targetInfo{};
        if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode)) {
            error = "An entry named " + targetFolder +
                    " already exists in the game's mods folder and is not a folder";
            return false;
        }
    }
    ModSyncLedger ledger;
    if (!loadModSyncLedger(install, ledger, error)) return false;
    const auto owned = findLedgerFolder(ledger, targetFolder);
    if (!recoverInterruptedModCopy(target, error)) return false;
    const bool unmanagedTarget = targetExists &&
        (owned == ledger.end() || !installedFolderMatchesOwnership(target, owned->second));
    if (unmanagedTarget && !adoptUnmanagedFolder) {
        error = "An installed folder named " + mod.folder +
                " already exists, but HQ does not manage it. Explicit ownership adoption is "
                "required before HQ can replace that folder.";
        return false;
    }
    std::string manifest;
    if (!readTextFile(source + "/modload.txt", manifest, 16 * 1024)) {
        error = "Could not read the SD mod's modload.txt";
        return false;
    }
    const std::string syncToken = modSyncTokenFromManifest(manifest);
    unsigned files = 0; unsigned long long bytes = 0;
    if (!countTreeFiles(source, 0, files, bytes) || files == 0) {
        error = "The SD mod is empty, unreadable, or exceeds safety limits";
        return false;
    }
    ModSyncLedger updated = ledger;
    if (owned != ledger.end()) updated.erase(owned->first);
    updated[targetFolder] = syncToken;
    const auto commit = [&](std::string& commitError) {
        std::string installedManifest;
        if (!readTextFile(target + "/modload.txt", installedManifest, 16 * 1024) ||
            modSyncTokenFromManifest(installedManifest) != syncToken) {
            commitError = "The mod manifest changed while it was being copied";
            return false;
        }
        return saveModSyncLedger(install, updated, commitError);
    };
    if (!copyModFolderTransactional(source, target, bytes, progress, commit, error))
        return false;
    if (progress) progress("Copying SD mod", "All mod files were copied successfully.", 100);
    return true;
}

bool copyInstalledModToSd(const GameInstall& install, const LocalMod& mod,
                          std::string& error, bool replaceExisting,
                          const ProgressCallback& progress) {
    error.clear();
    if (!install.update.present || !install.destinationReady) {
        error = "The game update is not available";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    if (!mod.installed || !validModFolder(mod.folder)) {
        error = "Invalid installed mod selection";
        return false;
    }
    const std::string source = install.update.root + "/content/mods/" + mod.folder;
    struct stat sourceInfo{};
    if (stat(source.c_str(), &sourceInfo) != 0 || !S_ISDIR(sourceInfo.st_mode)) {
        error = "The selected console mod folder no longer exists";
        return false;
    }
    std::string manifest;
    if (!readTextFile(source + "/modload.txt", manifest, 16 * 1024)) {
        error = "Could not read the console mod's modload.txt";
        return false;
    }
    if (!ensureDirectory(config::gameSdModsRoot())) {
        error = "Could not create sd:/unbound/content/mods";
        return false;
    }
    if (!reconcileInterruptedModDeletes(config::gameSdModsRoot(), error)) return false;
    bool targetExists = false;
    std::string targetFolder;
    if (!findCaseInsensitiveChild(config::gameSdModsRoot(), mod.folder, targetExists,
                                  targetFolder, error))
        return false;
    if (!targetExists) targetFolder = mod.folder;
    const std::string target = std::string(config::gameSdModsRoot()) + "/" + targetFolder;
    if (targetExists) {
        struct stat targetInfo{};
        if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode)) {
            error = "An SD-card entry named " + targetFolder +
                    " already exists and is not a folder";
            return false;
        }
        if (!replaceExisting) {
            error = "An SD mod folder named " + targetFolder +
                    " already exists. Confirm replacement before copying.";
            return false;
        }
    }
    unsigned files = 0;
    unsigned long long bytes = 0;
    if (!countTreeFiles(source, 0, files, bytes) || files == 0) {
        error = "The console mod is empty, unreadable, or exceeds safety limits";
        return false;
    }
    const auto copyProgress = [&](const std::string&, const std::string& detail,
                                  int percent) {
        if (progress) progress("Copying mod to SD", detail, percent);
    };
    if (!copyModFolderTransactional(source, target, bytes, copyProgress, {}, error))
        return false;
    if (progress) progress("Copying mod to SD",
                           "The mod is now available to Unbound Mod Link.", 100);
    return true;
}

bool deleteInstalledMod(const GameInstall& install, const LocalMod& mod, std::string& error,
                        const ProgressCallback& progress) {
    if (!install.update.present || !install.destinationReady) {
        error = "Choose a valid install destination before deleting a console mod.";
        return false;
    }
    if (!revalidateInstallDestination(install, error)) return false;
    if (!mod.installed || !validModFolder(mod.folder)) { error = "Invalid installed mod selection"; return false; }
    const std::string modsRoot = install.update.root + "/content/mods";
    if (!reconcileInterruptedModDeletes(modsRoot, error)) return false;
    bool targetExists = false;
    std::string targetFolder;
    if (!findCaseInsensitiveChild(modsRoot, mod.folder, targetExists,
                                  targetFolder, error))
        return false;
    if (!targetExists) {
        error = "The selected console mod folder no longer exists";
        return false;
    }
    const std::string target = modsRoot + "/" + targetFolder;
    struct stat targetInfo{};
    if (stat(target.c_str(), &targetInfo) != 0 || !S_ISDIR(targetInfo.st_mode)) {
        error = "The selected console mod is not a folder";
        return false;
    }
    if (progress) progress("Removing " + mod.name,
                           "Preserving the folder for safe deletion...", 20);
    ModSyncLedger ledger;
    if (!loadModSyncLedger(install, ledger, error)) return false;
    const auto owned = findLedgerFolder(ledger, targetFolder);
    bool pendingExists = false;
    std::string pendingLeaf;
    if (!findCaseInsensitiveChild(modsRoot, targetFolder + ".unbound-delete",
                                  pendingExists, pendingLeaf, error))
        return false;
    const std::string pendingDelete = target + ".unbound-delete";
    if (pendingExists) {
        error = "A reserved deletion folder already exists. HQ left it untouched; inspect " +
                modsRoot + "/" + pendingLeaf + " before retrying.";
        return false;
    }
    if (std::rename(target.c_str(), pendingDelete.c_str()) != 0) {
        error = "Could not preserve the mod folder for safe deletion";
        return false;
    }
    if (owned != ledger.end()) {
        ModSyncLedger updated = ledger;
        updated.erase(owned->first);
        if (!saveModSyncLedger(install, updated, error)) {
            if (std::rename(pendingDelete.c_str(), target.c_str()) != 0)
                error += "\nThe mod folder is still at " + targetFolder +
                         ".unbound-delete and must be restored manually.";
            return false;
        }
    }
    if (progress) progress("Removing " + mod.name,
                           "Deleting the preserved mod folder...", 60);
    if (!removeTree(pendingDelete)) {
        error = owned == ledger.end()
            ? "The mod was removed from the active list, but its temporary deletion folder could not be cleared"
            : "The mod was removed from HQ's ownership record, but its temporary deletion folder could not be cleared";
        return false;
    }
    if (progress) progress("Removing " + mod.name, "The mod was removed successfully.", 100);
    return true;
}

bool cleanupDownloadCache(const std::vector<std::string>& activeNewsImages,
                          unsigned long long& bytesFreed, std::string& error) {
    bytesFreed = 0;
    if (!ensureDirectory(config::gameCacheRoot())) { error = "Could not open the HQ cache"; return false; }
    std::unordered_set<std::string> keep(activeNewsImages.begin(), activeNewsImages.end());
    CoreRecoveryRecord repair;
    std::string repairError;
    const auto repairState=loadCoreRecovery(repair,repairError,false);
    if(repairState==CoreRecoveryLoadStatus::Pending){
        for(const auto& path:coreRecoveryProtectedCachePaths(repair))keep.insert(path);
    }
    // A locally chosen ZIP may itself be in HQ's cache. Never discard the
    // original source needed to finish an interrupted custom installation.
    std::string localText,localError;LocalPackagePending localPending;
    const bool localReadable=readInstallState(std::string(config::gameStateRoot())+"/local-package-install.txt",localText,localError);
    const bool localUnknown=!localReadable||(!localText.empty()&&!parseLocalPackageJournal(localText,localPending,localError));
    if(!localUnknown&&!localText.empty())keep.insert(localPending.package.path);
    DIR* directory = opendir(config::gameCacheRoot());
    if (!directory) { error = "Could not inspect the HQ cache"; return false; }
    bool ok = true;
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        const std::string path = std::string(config::gameCacheRoot()) + "/" + name;
        // Retain exact pending package identities. An unreadable journal must
        // never cause cleanup to destroy its possible repair download.
        if(localUnknown||keep.find(path)!=keep.end()||
           (repairState==CoreRecoveryLoadStatus::Invalid&&name.rfind("core-",0)==0))continue;
        struct stat info{};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        const bool partial = name.size() >= 5 && name.rfind(".part") == name.size() - 5;
        const bool resumeSidecar=name.size()>=12&&name.rfind(".part.resume")==name.size()-12;
        ResumeRecord saved;
        // Retain only a bounded regular partial with a matching resume record.
        // Its prefix digest/request identity are checked when the user retries,
        // never during startup cache cleanup.
        if(partial&&readResumePair(path,config::MaxArchiveBytes,saved))continue;
        if(resumeSidecar&&readResumePair(path.substr(0,path.size()-7),config::MaxArchiveBytes,saved))continue;
        const bool package = name.size() >= 4 && name.rfind(".zip") == name.size() - 4;
        const bool originalNews=name.rfind("news-",0)==0&&name.size()>=4&&name.rfind(".img")==name.size()-4;
        const bool scaledNews=name.rfind("news-",0)==0&&name.size()>=8&&name.rfind(".img.bmp")==name.size()-8;
        const std::string activePath=scaledNews?path.substr(0,path.size()-4):path;
        const bool staleNews=(originalNews||scaledNews)&&keep.find(activePath)==keep.end();
        if (!(partial || resumeSidecar || package || staleNews)) continue;
        if (std::remove(path.c_str()) == 0) bytesFreed += static_cast<unsigned long long>(info.st_size);
        else ok = false;
    }
    closedir(directory);
    if (!ok) error = "Some stale HQ cache files could not be removed";
    return ok;
}

} // namespace unbound
