#pragma once

#include "model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace unbound {

// This is a small SD-side reminder, not a copy of the game files. Keep the
// exact package chain so an interrupted install survives release-feed changes.
enum class CoreRecoveryPhase { BeforeDownload, Installing, Completed };
enum class CoreRecoveryLoadStatus { Missing, Pending, Invalid };
enum class CoreRecoveryReconcile { RetryRequired, Advanced, Completed, Conflict };

struct CoreRecoveryRecord {
    Device device = Device::Missing;
    std::uint64_t titleId = 0;
    std::string language;
    std::vector<Release> plan;
    std::size_t currentStep = 0;
    CoreRecoveryPhase phase = CoreRecoveryPhase::BeforeDownload;
    std::string beforeVersion;
    bool replayFull = false;
};

inline constexpr std::size_t MaxCoreRecoverySteps = 32;
inline constexpr std::size_t MaxCoreRecoveryBytes = 128 * 1024;

bool validateCoreRecovery(const CoreRecoveryRecord& record, std::string& error);
bool beginCoreRecovery(const GameInstall& install,
                       const std::vector<const Release*>& plan, bool replayFull,
                       CoreRecoveryRecord& record, std::string& error);
// Explicit fresh repair of a readable record whose installed marker conflicts.
// The replacement must use its exact destination and a full forward chain.
// Preparing is pure: reset
// only the replacement plan's cache first, then atomically save this record
// before downloading or changing game files. On either failure retain the old
// record. Installing deliberately keeps Launch Game blocked during the fresh
// download even when the version marker was changed by another installer.
bool prepareCoreRecoveryRestart(const CoreRecoveryRecord& previous,
                                 const GameInstall& install,
                                 const std::vector<const Release*>& fullPlan,
                                 CoreRecoveryRecord& replacement, std::string& error);
bool parseCoreRecovery(const std::string& text, CoreRecoveryRecord& record,
                       std::string& error);
bool serializeCoreRecovery(const CoreRecoveryRecord& record, std::string& text,
                           std::string& error);
std::string coreRecoveryTargetRoot(const CoreRecoveryRecord& record);
InstallPreferences coreRecoveryPreferences(const CoreRecoveryRecord& record);
std::string coreRecoveryArchivePath(const Release& release);
std::vector<std::string> coreRecoveryProtectedCachePaths(const CoreRecoveryRecord& record);
bool coreRecoveryMatchesInstall(const CoreRecoveryRecord& record,
                                const GameInstall& install);
bool coreRecoveryNeedsRepair(const CoreRecoveryRecord& record);
bool canDiscardCoreRecoveryDownload(const CoreRecoveryRecord& record);
// Correct only the current unfinished package's declared download size. Its
// exact release identity, URL, hash, phase and captured chain remain immutable;
// an Installing record must still repair that same package. Persist the
// returned record before using a changed size for a download or repair retry.
bool refreshCoreRecoveryDownloadSize(CoreRecoveryRecord& record,
                                     const std::vector<Release>& releases,
                                     bool& changed, std::string& error);

// Only Installing may reconcile a committed marker across the journal-save
// crash window. Equal-version full reinstalls must retry; their old and new
// markers are indistinguishable. Never infer completion from a newer feed.
CoreRecoveryReconcile reconcileCoreRecovery(CoreRecoveryRecord& record,
                                            const std::string& diskVersion,
                                            std::string& error);
bool markCoreRecoveryInstalling(CoreRecoveryRecord& record, std::string& error);
bool advanceCoreRecovery(CoreRecoveryRecord& record, const std::string& committedVersion,
                         std::string& error);

std::string coreRecoveryStatePath();
CoreRecoveryLoadStatus loadCoreRecovery(CoreRecoveryRecord& record, std::string& error,
                                        bool allowRecoveryWrites = true);
bool saveCoreRecovery(const CoreRecoveryRecord& record, std::string& error);
// Reset only the replacement chain's hash-named cache artifacts, then atomically
// replace the exact saved previous record. Never clear its repair gate first.
bool restartCoreRecovery(const CoreRecoveryRecord& previous,
                          const CoreRecoveryRecord& replacement, std::string& error);
// Call only after the exact plan completed. A Pending/Invalid record is never
// discarded by this function, and automatic cache cleanup must retain its ZIPs.
bool clearCoreRecovery(const CoreRecoveryRecord& record, std::string& error);
// Explicit user cancellation only. The first package has not begun writing any
// game file. Do not use this to forget an install or a partially completed chain.
bool discardCoreRecoveryDownload(const CoreRecoveryRecord& record, std::string& error);

} // namespace unbound

// Portable validation and transition helpers are shared by the Wii U build
// and host regression tests; the state-file I/O remains in core_recovery.cpp.
#include "core_recovery_record.hpp"
