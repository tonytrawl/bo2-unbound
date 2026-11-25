#pragma once

#include "model.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace unbound {

// Capture caller-owned session state without re-running discovery, recovery,
// network checks or plugin integrity scans. Collection is explicitly on demand.
struct DiagnosticsContext {
    GameInstall install;
    InstallPreferences installPreferences;
    bool contentUpdatesAllowed = true;
    bool modLinkEnabled = false;
    std::string latestUnboundVersion;
    std::string releaseCheckStatus;
    std::string latestHqVersion;
    std::uint64_t latestHqRevision = 0;
    std::string lastError;
    std::string sessionWarning;
    std::string coreRecoveryStatus;
    std::string inheritedRepairStatus;
    bool pluginRestartRequired = false;
    std::vector<WorkshopPlugin> plugins;
};

enum class DiagnosticsPathKind { Missing, File, Directory, Unavailable };

struct DiagnosticsPathInfo {
    DiagnosticsPathKind kind = DiagnosticsPathKind::Unavailable;
    std::uint64_t bytes = 0;
};

struct DiagnosticsDirectoryInfo {
    bool available = false;
    bool complete = false;
    unsigned files = 0;
    unsigned folders = 0;
    unsigned otherEntries = 0;
    std::uint64_t fileBytes = 0;
};

// Injectable read-only I/O allows host tests to prove that collecting a report
// has no network/mutation path. Directory counts are shallow and entry bounded.
struct DiagnosticsIo {
    std::function<DiagnosticsPathInfo(const std::string&)> inspectPath;
    std::function<bool(const std::string&, std::string&, std::size_t)> readText;
    std::function<bool(const std::string&, std::uint64_t&)> freeSpace;
    std::function<DiagnosticsDirectoryInfo(const std::string&, unsigned)> inspectDirectory;
};

struct DiagnosticsPerformanceRecord {
    std::uint64_t revision = 0;
    std::string operation;
    bool success = false;
    std::string mode;
    // Empty for older logs; otherwise one of manifest/head/unknown only.
    std::string sizeSource;
    std::map<std::string, std::uint64_t> values;
};

struct DiagnosticsReport {
    std::string text;
};

inline constexpr std::size_t DiagnosticsMaximumLogBytes = 64 * 1024;
inline constexpr std::size_t DiagnosticsMaximumInstallFailureBytes = 8 * 1024;
inline constexpr unsigned DiagnosticsMaximumDirectoryEntries = 128;

// These helpers are pure, bounded and independently host-testable. Log records
// retain recognized numeric measurements and bounded mode/source enums only;
// arbitrary SD text is not exported.
std::vector<DiagnosticsPerformanceRecord> parseDiagnosticsPerformanceLog(const std::string& text);
std::string formatDiagnosticsPerformanceRecord(const DiagnosticsPerformanceRecord& record);
std::string diagnosticsSafeMessage(const std::string& message);

DiagnosticsIo defaultDiagnosticsIo();
DiagnosticsReport collectDiagnostics(const DiagnosticsContext& context, const DiagnosticsIo& io);
DiagnosticsReport collectDiagnostics(const DiagnosticsContext& context);

} // namespace unbound
