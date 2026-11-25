#include "diagnostics.hpp"
#include "config.hpp"

#if defined(__WIIU__)
#include <coreinit/filesystem.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <utility>

namespace unbound {
namespace {

constexpr unsigned MaximumRetainedRecords = 128;
constexpr unsigned MaximumRawRecords = 16;
constexpr unsigned MaximumPlugins = 8;

bool parseNumber(const std::string& text, std::uint64_t& value) {
    if (text.empty()) return false;
    value = 0;
    for (const unsigned char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool recognizedMetric(const std::string& key) {
    for (const char* allowed : {"total_ms", "curl_ms", "write_ms", "hash_ms",
                               "queue_wait_ms", "bytes", "backup_ms", "zip_ms",
                               "close_ms", "zip_bytes", "backup_bytes", "resumed_bytes",
                               "rehash_ms", "network_bytes", "probe_ms", "pipelined_files",
                               "sync_files", "input_buffer_bytes", "planned_bytes", "required_bytes"})
        if (key == allowed) return true;
    return false;
}

bool validSizeSource(const std::string& value) {
    return value == "manifest" || value == "head" || value == "unknown";
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string decimalBytes(std::uint64_t bytes) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / 1000000.0;
    return output.str() + " MB (" + std::to_string(bytes) + " bytes)";
}

std::string titleId(std::uint64_t value) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

DiagnosticsPathInfo inspect(const DiagnosticsIo& io, const std::string& path) {
    if (path.empty() || !io.inspectPath) return {};
    return io.inspectPath(path);
}

std::string pathStatus(const DiagnosticsPathInfo& info) {
    switch (info.kind) {
    case DiagnosticsPathKind::Missing: return "Not present";
    case DiagnosticsPathKind::File: return "File present; " + decimalBytes(info.bytes);
    case DiagnosticsPathKind::Directory: return "Folder present";
    default: return "Unavailable (not mounted or unreadable)";
    }
}

bool isPresent(const DiagnosticsPathInfo& info) {
    return info.kind == DiagnosticsPathKind::File || info.kind == DiagnosticsPathKind::Directory;
}

void location(std::ostringstream& report, const char* label, const InstallLocation& value) {
    report << label << ": " << (value.present ? deviceName(value.device) : "Not found") << '\n';
    report << label << " path: " << (value.root.empty() ? "Unavailable" : value.root) << '\n';
}

void space(std::ostringstream& report, const char* label, const std::string& path,
           const DiagnosticsIo& io) {
    std::uint64_t available = 0;
    report << label << " free: ";
    // A successful free-space query alone does not establish an HQ-mounted
    // devoptab; require an accessible path before issuing the native FS query.
    if (isPresent(inspect(io, path)) && io.freeSpace && io.freeSpace(path, available))
        report << decimalBytes(available);
    else report << "Unavailable (not mounted or unreadable)";
    report << '\n';
}

void folder(std::ostringstream& report, const char* label, const std::string& path,
            const DiagnosticsIo& io) {
    report << label << " path: " << path << '\n';
    const auto state = inspect(io, path);
    if (state.kind == DiagnosticsPathKind::Missing) {
        report << label << ": Not present\n";
        return;
    }
    if (state.kind != DiagnosticsPathKind::Directory || !io.inspectDirectory) {
        report << label << ": Unavailable (not mounted or unreadable)\n";
        return;
    }
    const auto info = io.inspectDirectory(path, DiagnosticsMaximumDirectoryEntries);
    if (!info.available) {
        report << label << ": Unavailable (directory could not be read)\n";
        return;
    }
    report << label << " top level: " << info.files << " files, " << info.folders
           << " folders, " << info.otherEntries << " other entries; "
           << decimalBytes(info.fileBytes) << " in top-level files";
    if (!info.complete) report << "; partial count (entry limit or unreadable entry)";
    report << "\nSubfolder contents were not scanned.\n";
}

std::string setting(const std::string& text, const std::string& wanted) {
    bool found = false;
    std::string result = "OFF (default)";
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || lower(trim(line.substr(0, equals))) != wanted) continue;
        if (found) return "Invalid duplicate setting; effective value must be checked by HQ";
        found = true;
        const auto value = lower(trim(line.substr(equals + 1)));
        if (value == "on" || value == "true" || value == "1") result = "ON";
        else if (value == "off" || value == "false" || value == "0") result = "OFF";
        else return "Invalid setting; effective value must be checked by HQ";
    }
    return result;
}

std::string pluginPath(const std::string& path) {
    static const std::string prefix = "wiiu/environments/aroma/";
    if (!safeRelativePath(path) || path.rfind(prefix, 0) != 0 || path.size() <= prefix.size()) return {};
    return std::string(config::AromaRoot) + "/" + path.substr(prefix.size());
}

void plugin(std::ostringstream& report, const WorkshopPlugin& value,
            const std::string& settingsText, const std::string& settingsState,
            const DiagnosticsIo& io) {
    if (!validId(value.key)) return;
    report << '\n' << diagnosticsSafeMessage(value.name) << " (" << value.key << ")\n";
    if (value.key == "PROTARIUM") report << "Availability: Available in Plugin Workshop\n";
    report << "Startup auto-update: ";
    if (value.key == "UNBOUND_MOD_LINK") report << "ON (HQ-managed requirement)\n";
    else if (!settingsState.empty()) report << settingsState << '\n';
    else report << setting(settingsText, "plugin_auto_update." + lower(value.key)) << '\n';

    const auto receiptPath = std::string(config::PluginStateRoot) + "/" + lower(value.key) + ".txt";
    report << "Receipt: " << receiptPath << '\n';
    PluginRelease receipt;
    std::string receiptText;
    bool receiptValid = false;
    const auto receiptInfo = inspect(io, receiptPath);
    if (receiptInfo.kind == DiagnosticsPathKind::File) {
        receiptValid = io.readText && io.readText(receiptPath, receiptText, config::MaxManifestBytes) &&
            parsePluginRelease(receiptText, value, receipt);
        if (receiptValid) report << "Managed receipt version: " << receipt.version << " / r"
                                 << receipt.revision << '\n';
        else report << "Managed receipt: Unreadable or invalid\n";
    } else if (receiptInfo.kind == DiagnosticsPathKind::Missing) report << "Managed receipt: Not present\n";
    else report << "Managed receipt: Unavailable or wrong file type\n";

    bool pending = false;
    for (const char* suffix : {".transaction", ".transaction.unbound-new", ".transaction.unbound-old",
                              ".preinstall", ".unbound-new", ".unbound-old"})
        pending = pending || isPresent(inspect(io, receiptPath + suffix));
    unsigned seen = 0;
    for (const auto& managed : value.managedPaths) {
        if (++seen > config::MaxPluginFiles) break;
        const auto path = pluginPath(managed);
        if (path.empty()) continue;
        const auto info = inspect(io, path);
        report << "File: " << path << '\n' << "Status: " << pathStatus(info);
        if (receiptValid) {
            const auto expected = std::find_if(receipt.files.begin(), receipt.files.end(),
                [&managed](const auto& file) { return file.archivePath == managed; });
            if (expected != receipt.files.end() && info.kind == DiagnosticsPathKind::File)
                report << (expected->bytes == info.bytes ? "; receipt size matches" : "; receipt SIZE MISMATCH");
        }
        report << '\n';
        pending = pending || isPresent(inspect(io, path + ".unbound-new")) ||
                  isPresent(inspect(io, path + ".unbound-old"));
    }
    report << "Local recovery files: " << (pending ? "Present; no recovery was performed" : "None detected") << '\n';
}

std::string duration(const DiagnosticsPerformanceRecord& record, const char* key) {
    const auto value = record.values.find(key);
    if (value == record.values.end()) return "Not recorded";
    std::ostringstream result;
    result << std::fixed << std::setprecision(3) << static_cast<double>(value->second) / 1000.0;
    return result.str() + " s (" + std::to_string(value->second) + " ms)";
}

std::string speed(const DiagnosticsPerformanceRecord& record, const char* key) {
    const auto networkBytes = record.values.find("network_bytes");
    const auto bytes = networkBytes == record.values.end() ? record.values.find("bytes") : networkBytes;
    const auto ms = record.values.find(key);
    if (bytes == record.values.end() || ms == record.values.end() || !ms->second) return "Unavailable";
    std::ostringstream result;
    result << std::fixed << std::setprecision(2)
           << static_cast<double>(bytes->second) / static_cast<double>(ms->second) / 1000.0;
    return result.str() + " MB/s";
}

void performance(std::ostringstream& report, const DiagnosticsPerformanceRecord& record) {
    report << "\nLatest " << record.operation << ": " << (record.success ? "Succeeded" : "Failed")
           << " / r" << record.revision << " / " << record.mode << '\n';
    const auto bytes = record.values.find("bytes");
    if (bytes != record.values.end()) report << "Bytes: " << decimalBytes(bytes->second) << '\n';
    report << "Total elapsed: " << duration(record, "total_ms") << '\n';
    if (record.operation == "download") {
        if (record.sizeSource == "manifest")
            report << "Package size source: Release manifest (no HEAD request)\n";
        else if (record.sizeSource == "head")
            report << "Package size source: HTTP HEAD probe\n";
        else if (record.sizeSource == "unknown")
            report << "Package size source: Unknown; bounded streaming fallback\n";
        if (record.values.count("probe_ms"))
            report << "Size probe: " << duration(record, "probe_ms") << '\n';
        report << "Transfer time: " << duration(record, "curl_ms") << '\n';
        report << "Average during transfer: " << speed(record, "curl_ms") << '\n';
        report << "Average over full phase: " << speed(record, "total_ms") << '\n';
        report << "SD writes: " << duration(record, "write_ms") << '\n';
        report << "SHA-256: " << duration(record, "hash_ms") << '\n';
        report << "Queue wait: " << duration(record, "queue_wait_ms") << '\n';
        const auto resumed = record.values.find("resumed_bytes");
        if (resumed != record.values.end()) report << "Resumed bytes: " << decimalBytes(resumed->second) << '\n';
        const auto network = record.values.find("network_bytes");
        if (network != record.values.end()) report << "Received this attempt: " << decimalBytes(network->second) << '\n';
        if (record.values.count("rehash_ms")) report << "Resume-prefix rehash: " << duration(record, "rehash_ms") << '\n';
    } else if(record.operation=="preserve") {
        const auto planned=record.values.find("planned_bytes");
        const auto required=record.values.find("required_bytes");
        if(planned!=record.values.end())report<<"Planned original-content copy: "<<decimalBytes(planned->second)<<'\n';
        if(required!=record.values.end())report<<"Required starting free space: "<<decimalBytes(required->second)<<'\n';
        report<<"Includes local copying and integrity checks; planned bytes are not a measurement of partial writes.\n";
    } else {
        if (record.values.count("backup_ms"))
            report << "Original-file backup (historical measurement): " << duration(record, "backup_ms") << '\n';
        report << "ZIP read/unpack: " << duration(record, "zip_ms") << '\n';
        report << "Destination writes: " << duration(record, "write_ms") << '\n';
        report << "File close/flush: " << duration(record, "close_ms") << '\n';
        if (record.values.count("queue_wait_ms"))
            report << "Install queue wait: " << duration(record, "queue_wait_ms") << '\n';
        const auto pipelined = record.values.find("pipelined_files");
        if (pipelined != record.values.end()) {
            report << "Pipelined files: " << pipelined->second << '\n';
            if (pipelined->second)
                report << "ZIP read/unpack and destination writes overlap for pipelined files. "
                          "Do not add these component times together.\n";
        }
        const auto synchronous = record.values.find("sync_files");
        if (synchronous != record.values.end())
            report << "Direct-write files: " << synchronous->second << '\n';
        const auto inputBuffer = record.values.find("input_buffer_bytes");
        if (inputBuffer != record.values.end())
            report << "ZIP input buffer: " << decimalBytes(inputBuffer->second) << '\n';
        for (const auto& field : {std::pair{"zip_bytes", "ZIP bytes"}, std::pair{"backup_bytes", "Backup bytes"}}) {
            const auto value = record.values.find(field.first);
            if (value != record.values.end()) report << field.second << ": " << decimalBytes(value->second) << '\n';
        }
    }
}

} // namespace

std::vector<DiagnosticsPerformanceRecord> parseDiagnosticsPerformanceLog(const std::string& text) {
    std::vector<DiagnosticsPerformanceRecord> records;
    // Logs are already bounded by HQ; protect against a replaced, oversized file
    // as well. Never parse more than 64 KiB or retain more than 128 records.
    std::istringstream stream(text.substr(0, DiagnosticsMaximumLogBytes));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() > 2048) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = splitEscaped(line, '|');
        DiagnosticsPerformanceRecord record;
        if (fields.size() < 5 || fields[0].size() < 2 || fields[0].front() != 'r' ||
            !parseNumber(fields[0].substr(1), record.revision) ||
            (fields[1] != "download" && fields[1] != "install" && fields[1] != "preserve")) continue;
        record.operation = fields[1];
        bool valid = true;
        bool sawOk = false;
        bool sawMode = false;
        bool sawSizeSource = false;
        for (std::size_t index = 2; index < fields.size(); ++index) {
            const auto equals = fields[index].find('=');
            if (equals == std::string::npos) { valid = false; break; }
            const auto key = fields[index].substr(0, equals);
            const auto value = fields[index].substr(equals + 1);
            if (key == "ok") {
                if (sawOk || (value != "0" && value != "1")) { valid = false; break; }
                sawOk = true;
                record.success = value == "1";
            } else if (key == "mode") {
                if (sawMode || (value != "pipeline" && value != "buffered" &&
                               value != "worker" && value != "synchronous")) { valid = false; break; }
                sawMode = true;
                record.mode = value;
            } else if (key == "size_source") {
                if (sawSizeSource || record.operation != "download" || !validSizeSource(value)) {
                    valid = false; break;
                }
                sawSizeSource = true;
                record.sizeSource = value;
            } else if (recognizedMetric(key)) {
                std::uint64_t number = 0;
                if (!parseNumber(value, number) || !record.values.emplace(key, number).second) { valid = false; break; }
            }
            // Unknown fields are deliberately omitted rather than exported.
        }
        if (!valid || !sawOk || !sawMode || !record.values.count("total_ms") ||
            !record.values.count(record.operation=="preserve"?"planned_bytes":"bytes")) continue;
        if (records.size() == MaximumRetainedRecords) records.erase(records.begin());
        records.push_back(std::move(record));
    }
    return records;
}

std::string formatDiagnosticsPerformanceRecord(const DiagnosticsPerformanceRecord& record) {
    if ((record.operation != "download" && record.operation != "install" && record.operation != "preserve") ||
        (record.mode != "pipeline" && record.mode != "buffered" &&
         record.mode != "worker" && record.mode != "synchronous") ||
        (!record.sizeSource.empty() &&
         (record.operation != "download" || !validSizeSource(record.sizeSource)))) return {};
    std::string text = "r" + std::to_string(record.revision) + "|" + record.operation +
        "|ok=" + (record.success ? "1" : "0");
    for (const auto& [key, value] : record.values)
        if (recognizedMetric(key)) text += "|" + key + "=" + std::to_string(value);
    if (!record.sizeSource.empty()) text += "|size_source=" + record.sizeSource;
    text += "|mode=" + record.mode;
    return text;
}

namespace {
std::string safeMessageBounded(const std::string& message,std::size_t maximum) {
    // Errors may contain third-party URLs. Omit URL credentials/query strings,
    // IP literals and common identity/token fields instead of exporting them.
    std::string value = message.substr(0, maximum);
    for (char& c : value)
        if ((static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t') || c == '\x7f') c = ' ';
    const auto folded = lower(value);
    std::string result;
    result.reserve(value.size());
    std::size_t position = 0;
    while (position < value.size()) {
        if (folded.compare(position, 8, "https://") == 0 || folded.compare(position, 7, "http://") == 0) {
            const auto end = value.find_first_of(" \t\r\n)", position);
            result += "[network URL omitted]";
            position = end == std::string::npos ? value.size() : end;
            continue;
        }
        bool sensitive = false;
        for (const char* key : {"token=", "token:", "nnid=", "nnid:", "pnid=", "pnid:",
                                "account_id=", "account_id:", "authorization:", "session="}) {
            if (folded.compare(position, std::char_traits<char>::length(key), key) == 0) {
                const auto end = value.find_first_of("\r\n", position);
                result += "[private value omitted]";
                position = end == std::string::npos ? value.size() : end;
                sensitive = true;
                break;
            }
        }
        if (sensitive) continue;
        if (std::isdigit(static_cast<unsigned char>(value[position])) &&
            (position == 0 || !std::isalnum(static_cast<unsigned char>(value[position - 1])))) {
            std::size_t end = position;
            unsigned segments = 0;
            bool valid = true;
            while (segments < 4) {
                const auto begin = end;
                unsigned number = 0;
                while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
                    number = number * 10 + static_cast<unsigned>(value[end] - '0');
                    ++end;
                    if (end - begin > 3) { valid = false; break; }
                }
                if (!valid || end == begin || number > 255) { valid = false; break; }
                ++segments;
                if (segments < 4) {
                    if (end == value.size() || value[end] != '.') { valid = false; break; }
                    ++end;
                }
            }
            if (valid && segments == 4 && (end == value.size() ||
                (!std::isalnum(static_cast<unsigned char>(value[end])) && value[end] != '.'))) {
                result += "[IP omitted]";
                position = end;
                continue;
            }
        }
        result.push_back(value[position]);
        ++position;
    }
    return result;
}
} // namespace

std::string diagnosticsSafeMessage(const std::string& message) {
    return safeMessageBounded(message,4096);
}

DiagnosticsIo defaultDiagnosticsIo() {
    DiagnosticsIo io;
    io.inspectPath = [](const std::string& path) {
        struct stat info{};
        if (stat(path.c_str(), &info) != 0)
            return DiagnosticsPathInfo{errno == ENOENT ? DiagnosticsPathKind::Missing : DiagnosticsPathKind::Unavailable, 0};
        if (S_ISDIR(info.st_mode)) return DiagnosticsPathInfo{DiagnosticsPathKind::Directory, 0};
        if (S_ISREG(info.st_mode) && info.st_size >= 0)
            return DiagnosticsPathInfo{DiagnosticsPathKind::File, static_cast<std::uint64_t>(info.st_size)};
        return DiagnosticsPathInfo{};
    };
    io.readText = [](const std::string& path, std::string& value, std::size_t maximum) {
        value.clear();
        FILE* input = std::fopen(path.c_str(), "rb");
        if (!input) return false;
        char buffer[4096];
        bool ok = true;
        while (const auto bytes = std::fread(buffer, 1, sizeof(buffer), input)) {
            if (bytes > maximum - value.size()) { ok = false; break; }
            value.append(buffer, bytes);
        }
        ok = ok && !std::ferror(input);
        if (std::fclose(input) != 0) ok = false;
        if (!ok) value.clear();
        return ok;
    };
    io.freeSpace = [](const std::string& path, std::uint64_t& available) {
        available = 0;
#if defined(__WIIU__)
        const char* volume = nullptr;
        if (path.rfind("fs:/vol/external01", 0) == 0) volume = "/vol/external01";
        else if (path.rfind("storage_mlc01:", 0) == 0) volume = "/vol/storage_mlc01";
        else if (path.rfind("storage_usb01:", 0) == 0) volume = "/vol/storage_usb01";
        if (!volume) return false;
        alignas(64) FSClient client{};
        alignas(64) FSCmdBlock block{};
        if (FSAddClient(&client, FS_ERROR_FLAG_NONE) < 0) return false;
        FSInitCmdBlock(&block);
        const bool ok = FSGetFreeSpaceSize(&client, &block, volume, &available, FS_ERROR_FLAG_NONE) >= 0;
        FSDelClient(&client, FS_ERROR_FLAG_NONE);
        return ok;
#else
        (void)path;
        return false;
#endif
    };
    io.inspectDirectory = [inspectPath = io.inspectPath](const std::string& path, unsigned maximum) {
        DiagnosticsDirectoryInfo info;
        DIR* directory = opendir(path.c_str());
        if (!directory) return info;
        info.available = true;
        info.complete = true;
        unsigned visited = 0;
        errno = 0;
        while (const auto* entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (visited++ >= maximum) { info.complete = false; break; }
            const auto child = inspectPath(path + "/" + name);
            if (child.kind == DiagnosticsPathKind::File) {
                ++info.files;
                if (child.bytes > std::numeric_limits<std::uint64_t>::max() - info.fileBytes) info.complete = false;
                else info.fileBytes += child.bytes;
            } else if (child.kind == DiagnosticsPathKind::Directory) ++info.folders;
            else { ++info.otherEntries; info.complete = false; }
            errno = 0;
        }
        if (errno) info.complete = false;
        if (closedir(directory) != 0) info.complete = false;
        return info;
    };
    return io;
}

DiagnosticsReport collectDiagnostics(const DiagnosticsContext& context, const DiagnosticsIo& io) {
    std::ostringstream report;
    const auto& install = context.install;
    report << "BLACK OPS 2 UNBOUND HQ / SUPPORT INFORMATION\n"
           << "Local read-only snapshot; no network requests or repair actions.\n"
           << "Accounts, network addresses and session tokens are not collected.\n"
           << "Sizes use decimal MB. File presence does not prove a plugin is loaded.\n\n"
           << "APPLICATION AND SESSION\n"
           << "HQ version: " << config::AppVersion << " / r" << config::AppReleaseRevision << '\n'
           << "HQ WUHB: " << config::AppWuhbPath << '\n'
           << "HQ file: " << pathStatus(inspect(io, config::AppWuhbPath)) << '\n'
           << "Latest known HQ: " << (context.latestHqVersion.empty() ? "Not provided" : diagnosticsSafeMessage(context.latestHqVersion));
    if (context.latestHqRevision) report << " / r" << context.latestHqRevision;
    report << "\nGame-content writes: " << (context.contentUpdatesAllowed ? "Allowed this session" : "Blocked / read-only this session") << '\n'
           << "Release check: " << (context.releaseCheckStatus.empty() ? "Not recorded" : diagnosticsSafeMessage(context.releaseCheckStatus)) << '\n'
           << "Latest known Unbound: " << (context.latestUnboundVersion.empty() ? "Unavailable" : diagnosticsSafeMessage(context.latestUnboundVersion)) << '\n'
           << "Last error: " << (context.lastError.empty() ? "None recorded" : diagnosticsSafeMessage(context.lastError)) << '\n'
           << "Session warning: " << (context.sessionWarning.empty() ? "None recorded" : diagnosticsSafeMessage(context.sessionWarning)) << '\n'
           << "Unfinished core install: " << (context.coreRecoveryStatus.empty() ? "None recorded" : diagnosticsSafeMessage(context.coreRecoveryStatus)) << '\n'
           << "Inherited-content repair: " << (context.inheritedRepairStatus.empty()?"No pending repair recorded":diagnosticsSafeMessage(context.inheritedRepairStatus)) << '\n'
           << "Plugin restart required (session): " << (context.pluginRestartRequired ? "Yes" : "No update installed in this session; loaded Aroma code is not inspected") << '\n';

    // A later Launch Game warning may replace the session's generic Last error.
    // Preserve the actual install failure independently across app restarts.
    // This bounded read is only part of explicit Support/report collection;
    // it adds no work to downloads, installation or normal startup.
    report << "Last installation failure (saved): ";
    const auto savedFailureInfo=inspect(io,config::gameInstallFailurePath());
    if(savedFailureInfo.kind==DiagnosticsPathKind::Missing){
        report << "None recorded\n";
    }else if(savedFailureInfo.kind!=DiagnosticsPathKind::File||
             savedFailureInfo.bytes>DiagnosticsMaximumInstallFailureBytes){
        report << "Unavailable (not a bounded readable text file)\n";
    }else{
        std::string savedFailure;
        if(!io.readText||!io.readText(config::gameInstallFailurePath(),savedFailure,
                                     DiagnosticsMaximumInstallFailureBytes)||
           savedFailure.size()>DiagnosticsMaximumInstallFailureBytes)
            report << "Unavailable (saved record could not be read)\n";
        else if(trim(savedFailure).empty())report << "Saved record is empty\n";
        else report << safeMessageBounded(savedFailure,DiagnosticsMaximumInstallFailureBytes) << '\n';
    }

    report << "\nGAME AND INSTALLATION\n"
           << "Console region: " << regionName(install.consoleRegion) << '\n'
           << "Detected game SKU: " << (install.sku ? install.sku->label : "Not found") << '\n';
    if (install.sku) {
        report << "SKU key: " << install.sku->key << '\n'
               << "Game region: " << regionName(install.sku->region) << '\n'
               << "Base title ID: " << titleId(install.sku->baseTitleId) << '\n'
               << "Update title ID: " << titleId(install.sku->updateTitleId) << '\n';
    }
    report << "Official game update: " << officialUpdateSummary(install) << '\n';
    const auto required = requiredOfficialUpdateVersion(install.sku);
    report << "Official update requirement: " << (required ? "v" + std::to_string(required) : "Unknown game") << '\n';
    location(report, "Base game", install.base);
    location(report, "Official update", install.update);
    report << "Base title version: " << (install.base.titleVersion ? "v" + std::to_string(install.base.titleVersion) : "Not determined") << '\n'
           << "Install destination mode: " << (install.targetOverridden ? "Manual" : "Auto") << '\n'
           << "Saved install target: " << installTargetKey(context.installPreferences.updateTitleId, context.installPreferences.updateDevice) << '\n'
           << "Language selection mode: " << (install.languageOverridden ? "Manual" : "Auto") << '\n'
           << "Saved language: " << (context.installPreferences.languageFolder.empty() ? "Auto" : diagnosticsSafeMessage(context.installPreferences.languageFolder)) << '\n'
           << "Install destination ready: " << (install.destinationReady ? "Yes" : "No - writes blocked") << '\n'
           << "Destination warning: " << (install.destinationWarning.empty() ? "None recorded" : diagnosticsSafeMessage(install.destinationWarning)) << '\n'
           << "Full language install pending: " << (context.installPreferences.languageInstallPending ? "Yes" : "No") << '\n'
           << "Language folder: " << diagnosticsSafeMessage(install.languageFolder) << '\n'
           << "Unbound version (session): " << (install.modVersion.empty() ? "Not installed / marker unavailable" : diagnosticsSafeMessage(install.modVersion)) << '\n'
           << "Install routing: update/ to USB/NAND, sd/ to game-specific SD content; no AOC payloads\n"
           << "Core ZIP download limit: " << config::MaxArchiveBytes << " bytes\n"
           << "Core expanded-package limit: " << config::MaxArchiveExpandedBytes << " bytes\n"
           << "Core individual-file limit: " << config::MaxArchiveEntryBytes << " bytes\n"
           << "Game-content backups: Disabled; no automatic rollback\n"
           << "Failed game-content install: Relaunch HQ and retry the same package before playing.\n"
           << "Version marker is written only after its package succeeds.\n"
           << "HQ application and Aroma plugin updater recovery: Retained\n";
    if (!install.update.root.empty()) {
        const auto marker = install.update.root + "/" + config::VersionRelativePath;
        report << "Unbound marker: " << marker << '\n'
               << "Marker status: " << pathStatus(inspect(io, marker)) << '\n';
        std::string version;
        if (io.readText && io.readText(marker, version, 65536))
            report << "Marker value (current disk): " << diagnosticsSafeMessage(trim(version)) << '\n';
        report << "Installed mods: " << install.update.root + "/content/mods" << '\n';
    }

    report << "\nOPTIONAL MAP PACKS (USB/NAND)\nCatalog: " << config::gameReleasesUrl() << '\n';
    for(const char* record:{"pending.txt","selection.txt"}){
        std::string value;const auto path=std::string(config::gameStateRoot())+"/maps/"+record;
        report << record << ": ";
        if(io.readText&&io.readText(path,value,8192))report << diagnosticsSafeMessage(value) << '\n';
        else report << "No readable record\n";
    }
    report << "\nLOCAL PACKAGE RECOVERY\n";
    const auto localRecord=std::string(config::gameStateRoot())+"/local-package-install.txt";
    report << "Record: " << localRecord << '\n';
    std::string localPending;
    if(io.readText&&io.readText(localRecord,localPending,4096))
        report << (trim(localPending).empty()?"No unfinished local package":diagnosticsSafeMessage(localPending)) << '\n';
    else report << "No readable record\n";
    report << "\nSTORAGE AND HQ FILES\n";
    space(report, "SD card", "fs:/vol/external01", io);
    space(report, "Internal memory", "storage_mlc01:/", io);
    space(report, "USB storage", "storage_usb01:/", io);
    if (!install.update.root.empty()) space(report, "Actual update destination", install.update.root, io);
    else report << "Actual update destination free: Unavailable (no installed update)\n";
    report << "HQ folder: " << config::SdRoot << '\n'
           << "Install settings: " << config::gameInstallSettingsPath() << '\n';
    folder(report, "HQ cache", config::gameCacheRoot(), io);
    folder(report, "Plugin cache", config::PluginCacheRoot, io);
    folder(report, "Legacy game-content backups", std::string(config::SdRoot) + "/backups", io);
    report << "Legacy backups are left untouched; no new game-content backups are created or automatically restored.\n"
           << "Directory inventory is shallow and limited to " << DiagnosticsMaximumDirectoryEntries << " entries per folder.\n";

    report << "\nMOD LINK AND AROMA\n"
           << "SD content loading: provided by SD-capable Unbound Core\n"
           << "SD mods: " << config::gameSdModsRoot() << '\n'
           << "SD mods status: " << pathStatus(inspect(io, config::gameSdModsRoot())) << '\n'
           << "Disable flag: " << pathStatus(inspect(io, config::ModLinkDisableFlagPath)) << '\n'
           << "Aroma folder: " << config::AromaRoot << '\n'
           << "Aroma path status: " << pathStatus(inspect(io, config::AromaRoot)) << '\n';
    for (const char* module : {"ContentRedirectionModule.wms", "NotificationModule.wms"}) {
        const auto path = std::string(config::AromaRoot) + "/modules/" + module;
        report << module << ": " << pathStatus(inspect(io, path)) << '\n';
    }
    report << "Runtime-loaded status and plugin hashes are not checked by this screen.\n";
    std::string settingsText;
    std::string settingsState;
    const auto settingsInfo = inspect(io, config::SettingsPath);
    if (settingsInfo.kind == DiagnosticsPathKind::File &&
        (!io.readText || !io.readText(config::SettingsPath, settingsText, config::MaxSettingsBytes)))
        settingsState = "Unavailable (settings could not be read)";
    else if (settingsInfo.kind == DiagnosticsPathKind::Unavailable ||
             settingsInfo.kind == DiagnosticsPathKind::Directory)
        settingsState = "Unavailable (settings inaccessible or wrong file type)";
    if (isPresent(inspect(io, std::string(config::SettingsPath) + ".unbound-new")) ||
        isPresent(inspect(io, std::string(config::SettingsPath) + ".unbound-old")))
        settingsState = "Settings recovery pending; files left unchanged";
    report << "\nWORKSHOP PLUGINS / LOCAL RECEIPTS\n";
    unsigned plugins = 0;
    for (const auto& value : context.plugins) {
        if (plugins++ >= MaximumPlugins) { report << "Additional plugins omitted (bounded report).\n"; break; }
        plugin(report, value, settingsText, settingsState, io);
    }
    if (context.plugins.empty()) report << "No plugin catalog was provided by the current session.\n";

    const auto logPath = std::string(config::SdRoot) + "/performance.log";
    report << "\nPERFORMANCE / MOST RECENT LOCAL MEASUREMENTS\n"
           << "Log: " << logPath << '\n'
           << "Measurements describe completed attempts, not current connection capacity.\n"
           << "Pipeline work overlaps: do not add download component times together.\n";
    std::string log;
    const auto records = io.readText && io.readText(logPath, log, DiagnosticsMaximumLogBytes)
        ? parseDiagnosticsPerformanceLog(log) : std::vector<DiagnosticsPerformanceRecord>{};
    for (const char* operation : {"download", "install", "preserve"}) {
        const auto record = std::find_if(records.rbegin(), records.rend(), [operation](const auto& value) {
            return value.operation == operation;
        });
        if (record != records.rend()) performance(report, *record);
        else report << "\nLatest " << operation << ": No readable measurement recorded\n";
    }
    report << "\nCPU load, live FPS, temperature and negotiated network link speed: Not measured.\n";
    if (!records.empty()) {
        report << "\nRECENT RAW MEASUREMENTS (recognized fields only; at most " << MaximumRawRecords << " records)\n";
        const auto begin = records.size() > MaximumRawRecords ? records.size() - MaximumRawRecords : 0;
        for (std::size_t index = begin; index < records.size(); ++index)
            report << formatDiagnosticsPerformanceRecord(records[index]) << '\n';
    }
    return {report.str()};
}

DiagnosticsReport collectDiagnostics(const DiagnosticsContext& context) {
    return collectDiagnostics(context, defaultDiagnosticsIo());
}

} // namespace unbound
