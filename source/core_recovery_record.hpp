#pragma once

#include "core_recovery.hpp"
#include "config.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace unbound {
namespace core_recovery_detail {

inline bool decimal(std::string_view text, std::uint64_t& output) {
    if (text.empty() || text.size() > 20) return false;
    std::uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<unsigned>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

inline bool version(std::string_view text) {
    if (text.empty() || text.size() > 64) return false;
    std::size_t position = 0;
    const auto component = [&]() {
        const auto start = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
        // compareVersions uses unsigned long on Wii U. Bound each component so
        // journal validation cannot silently overflow its comparison routine.
        return position > start && position - start <= 9;
    };
    for (unsigned index = 0; index < 3; ++index) {
        if (!component()) return false;
        if (index < 2 && (position == text.size() || text[position++] != '.')) return false;
    }
    if (position == text.size()) return true;
    if (text[position++] != 'P') return false;
    do {
        if (!component()) return false;
        if (position == text.size()) return true;
        if (text[position++] != '.') return false;
    } while (position < text.size());
    return false;
}

inline bool digest(std::string_view text) {
    if (text.size() != 64) return false;
    for (const char ch : text)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    return true;
}

inline bool https(std::string_view text) {
    if (text.size() > 2048 || text.rfind("https://", 0) != 0) return false;
    const auto authorityEnd = text.find_first_of("/?#", 8);
    const auto authority = text.substr(8, authorityEnd == std::string_view::npos ?
                                          std::string_view::npos : authorityEnd - 8);
    if (authority.empty() || authority.find('@') != std::string_view::npos) return false;
    for (const unsigned char ch : text)
        if (ch <= 0x20 || ch >= 0x7f || ch == '|' || ch == '\\' || ch == '#') return false;
    return true;
}

inline bool marker(std::string_view text) {
    return text == "Not installed" || version(text);
}

inline std::string folded(std::string value) {
    for (char& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}

inline const GameSku* sku(const CoreRecoveryRecord& record) {
    const auto* found = findSkuByTitle(record.titleId);
    return found && found->updateTitleId == record.titleId ? found : nullptr;
}

inline bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

} // namespace core_recovery_detail

inline bool validateCoreRecovery(const CoreRecoveryRecord& record, std::string& error) {
    using namespace core_recovery_detail;
    error.clear();
    const auto* selected = sku(record);
    if ((record.device != Device::MLC && record.device != Device::USB) || !selected ||
        !supportedLanguageFolder(record.language))
        return fail(error, "The repair record has an unsupported destination or language.");
    if (record.plan.empty() || record.plan.size() > MaxCoreRecoverySteps ||
        record.currentStep > record.plan.size() || !marker(record.beforeVersion))
        return fail(error, "The repair record has an invalid package count, step or version marker.");
    if (record.phase != CoreRecoveryPhase::BeforeDownload && record.phase != CoreRecoveryPhase::Installing &&
        record.phase != CoreRecoveryPhase::Completed)
        return fail(error, "The repair record has an invalid operation phase.");
    if ((record.phase == CoreRecoveryPhase::Completed) != (record.currentStep == record.plan.size()))
        return fail(error, "The repair record's completion state does not match its package step.");
    if (record.replayFull && record.plan.front().kind != ReleaseKind::Full)
        return fail(error, "The language reinstall repair record must begin with a full package.");
    std::string previous;
    for (std::size_t index = 0; index < record.plan.size(); ++index) {
        const auto& release = record.plan[index];
        if ((release.regionKey != "UNIVERSAL" && release.regionKey != selected->key) ||
            !version(release.version) || !digest(release.sha256) || !https(release.downloadUrl) ||
            release.compressedBytes > config::MaxArchiveBytes)
            return fail(error, "The repair record contains invalid or incompatible package metadata.");
        if (release.kind == ReleaseKind::Full) {
            if (!release.fromVersion.empty()) return fail(error, "A full repair package has a patch prerequisite.");
        } else if (release.kind == ReleaseKind::Patch) {
            if (!version(release.fromVersion) || compareVersions(release.fromVersion, release.version) >= 0)
                return fail(error, "A repair patch has an invalid version prerequisite.");
        } else return fail(error, "The repair record contains an unknown package type.");
        if (index && !releaseAppliesToInstalledVersion(release, previous))
            return fail(error, "The repair package chain is not a compatible forward update.");
        previous = release.version;
    }
    if (record.currentStep && record.beforeVersion != record.plan[record.currentStep - 1].version)
        return fail(error, "The repair step's previous marker does not match the saved package chain.");
    if (!record.currentStep) {
        if (!record.replayFull && !releaseAppliesToInstalledVersion(record.plan.front(), record.beforeVersion))
            return fail(error, "The first repair package is incompatible with the saved installed version.");
        if (record.replayFull && record.beforeVersion != "Not installed" &&
            compareVersions(record.beforeVersion, record.plan.back().version) > 0)
            return fail(error, "The saved reinstall would downgrade an installed newer release.");
    }
    return true;
}

inline bool beginCoreRecovery(const GameInstall& install,
                              const std::vector<const Release*>& plan, bool replayFull,
                              CoreRecoveryRecord& record, std::string& error) {
    CoreRecoveryRecord next;
    if (!install.destinationReady || !install.update.present || !install.sku)
        return core_recovery_detail::fail(error, "The selected repair destination is unavailable.");
    next.device = install.update.device;
    next.titleId = install.sku->updateTitleId;
    next.language = install.languageFolder;
    next.beforeVersion = install.modVersion.empty() ? "Not installed" : install.modVersion;
    next.replayFull = replayFull;
    if (plan.empty() || plan.size() > MaxCoreRecoverySteps)
        return core_recovery_detail::fail(error, "The install plan is empty or too large to remember safely.");
    for (const auto* release : plan) {
        if (!release) return core_recovery_detail::fail(error, "The install plan contains a missing package.");
        next.plan.push_back(*release);
    }
    if (!validateCoreRecovery(next, error) || !coreRecoveryMatchesInstall(next, install)) {
        if (error.empty()) error = "The selected title path does not match its supported install destination.";
        return false;
    }
    record = std::move(next);
    return true;
}

inline bool prepareCoreRecoveryRestart(const CoreRecoveryRecord& previous,
                                        const GameInstall& install,
                                        const std::vector<const Release*>& fullPlan,
                                        CoreRecoveryRecord& replacement, std::string& error) {
    if (!validateCoreRecovery(previous, error)) return false;
    if (!coreRecoveryMatchesInstall(previous, install))
        return core_recovery_detail::fail(error, "Download fresh must use the unfinished installation's exact storage, title and language.");
    CoreRecoveryRecord next;
    // replayFull also validates the destination marker and disallows finishing
    // below its current version. Never turn an invalid marker into a blank one.
    if (!beginCoreRecovery(install, fullPlan, true, next, error)) return false;
    // Discovery resolves a unique existing folder spelling. Keep the journal's
    // original spelling immutable when a case-only selection alias is used.
    next.language = previous.language;
    if (compareVersions(next.plan.back().version, previous.plan.back().version) < 0)
        return core_recovery_detail::fail(error, "The newest complete package chain is older than the unfinished installation. Its repair record was retained.");
    // Some old payload files may already have been replaced. A cancelled fresh
    // download must not reopen Launch Game simply because its step is zero.
    next.phase = CoreRecoveryPhase::Installing;
    if (!validateCoreRecovery(next, error)) return false;
    replacement = std::move(next);
    return true;
}

inline std::string coreRecoveryTargetRoot(const CoreRecoveryRecord& record) {
    if (!core_recovery_detail::sku(record) ||
        (record.device != Device::MLC && record.device != Device::USB)) return {};
    const auto target = installTargetKey(record.titleId, record.device);
    return std::string(record.device == Device::USB ? "storage_usb01:/usr/title/" :
                                                    "storage_mlc01:/usr/title/") +
           target.substr(4, 8) + "/" + target.substr(12, 8);
}

inline InstallPreferences coreRecoveryPreferences(const CoreRecoveryRecord& record) {
    return {record.device, record.titleId, record.language, record.replayFull};
}

inline std::string coreRecoveryArchivePath(const Release& release) {
    if (!core_recovery_detail::digest(release.sha256)) return {};
    return std::string(config::gameCacheRoot()) + "/core-" +
           core_recovery_detail::folded(release.sha256) + ".zip";
}

inline std::vector<std::string> coreRecoveryProtectedCachePaths(const CoreRecoveryRecord& record) {
    std::string error;
    if (!validateCoreRecovery(record, error)) return {};
    std::vector<std::string> paths;
    for (std::size_t index = record.currentStep; index < record.plan.size(); ++index) {
        const auto path = coreRecoveryArchivePath(record.plan[index]);
        paths.push_back(path);
        paths.push_back(path + ".part");
        paths.push_back(path + ".part.resume");
    }
    return paths;
}

inline bool coreRecoveryMatchesInstall(const CoreRecoveryRecord& record, const GameInstall& install) {
    return install.destinationReady && install.update.present && install.sku &&
           install.sku->updateTitleId == record.titleId && install.update.device == record.device &&
           !coreRecoveryTargetRoot(record).empty() && install.update.root == coreRecoveryTargetRoot(record) &&
           core_recovery_detail::folded(install.languageFolder) == core_recovery_detail::folded(record.language);
}

inline bool coreRecoveryNeedsRepair(const CoreRecoveryRecord& record) {
    return record.phase == CoreRecoveryPhase::Installing ||
           (record.replayFull && record.currentStep > 0 && record.phase != CoreRecoveryPhase::Completed);
}

inline bool canDiscardCoreRecoveryDownload(const CoreRecoveryRecord& record) {
    std::string error;
    return validateCoreRecovery(record, error) && record.currentStep == 0 &&
           record.phase == CoreRecoveryPhase::BeforeDownload;
}

inline bool refreshCoreRecoveryDownloadSize(CoreRecoveryRecord& record,
                                            const std::vector<Release>& releases,
                                            bool& changed, std::string& error) {
    changed = false;
    if (!validateCoreRecovery(record, error)) return false;
    // A size typo is not a different payload when every identity field and
    // its SHA-256 stay exact. Installing still means repair: preserve its
    // phase, marker and step, and retry that same archive without inferring
    // whether its earlier attempt replaced any files. Completed is immutable.
    if (record.phase == CoreRecoveryPhase::Completed) return true;
    const auto& current = record.plan[record.currentStep];
    const Release* published = nullptr;
    for (const auto& candidate : releases) {
        if (candidate.regionKey != current.regionKey || candidate.kind != current.kind ||
            candidate.fromVersion != current.fromVersion || candidate.version != current.version ||
            candidate.downloadUrl != current.downloadUrl || candidate.sha256 != current.sha256)
            continue;
        if (!candidate.compressedBytes || candidate.compressedBytes > config::MaxArchiveBytes)
            return core_recovery_detail::fail(error,
                "The published size for this saved package is invalid. Its repair record was retained.");
        if (published && published->compressedBytes != candidate.compressedBytes)
            return core_recovery_detail::fail(error,
                "The release feed gives conflicting sizes for this saved package. Its repair record was retained.");
        published = &candidate;
    }
    if (!published || published->compressedBytes == current.compressedBytes) return true;
    // The payload hash is unchanged: a publisher may have corrected a KB/bytes
    // typo without authorizing a different archive, target, version or chain.
    auto corrected = record;
    corrected.plan[corrected.currentStep].compressedBytes = published->compressedBytes;
    if (!validateCoreRecovery(corrected, error)) return false;
    record = std::move(corrected);
    changed = true;
    return true;
}

inline bool markCoreRecoveryInstalling(CoreRecoveryRecord& record, std::string& error) {
    if (!validateCoreRecovery(record, error)) return false;
    if (record.phase == CoreRecoveryPhase::Completed)
        return core_recovery_detail::fail(error, "The saved package chain is already complete.");
    record.phase = CoreRecoveryPhase::Installing;
    return true;
}

inline bool advanceCoreRecovery(CoreRecoveryRecord& record, const std::string& committedVersion,
                                std::string& error) {
    if (!validateCoreRecovery(record, error)) return false;
    if (record.phase != CoreRecoveryPhase::Installing ||
        committedVersion != record.plan[record.currentStep].version)
        return core_recovery_detail::fail(error, "The committed version does not match the active repair package.");
    record.beforeVersion = committedVersion;
    ++record.currentStep;
    record.phase = record.currentStep == record.plan.size() ? CoreRecoveryPhase::Completed :
                                                            CoreRecoveryPhase::BeforeDownload;
    return true;
}

inline CoreRecoveryReconcile reconcileCoreRecovery(CoreRecoveryRecord& record,
                                                   const std::string& diskVersion,
                                                   std::string& error) {
    if (!validateCoreRecovery(record, error)) return CoreRecoveryReconcile::Conflict;
    const std::string disk = diskVersion.empty() ? "Not installed" : diskVersion;
    if (!core_recovery_detail::marker(disk)) {
        error = "The destination's installed version marker is invalid. Repair cannot be chosen safely.";
        return CoreRecoveryReconcile::Conflict;
    }
    if (record.phase == CoreRecoveryPhase::Completed) {
        if (disk == record.beforeVersion) return CoreRecoveryReconcile::Completed;
    } else if (disk == record.beforeVersion) {
        // This branch intentionally precedes the expected-new-version branch:
        // retry a same-version replay even though the shared marker is current.
        return CoreRecoveryReconcile::RetryRequired;
    } else if (record.phase == CoreRecoveryPhase::Installing &&
               disk == record.plan[record.currentStep].version) {
        if (advanceCoreRecovery(record, disk, error))
            return record.phase == CoreRecoveryPhase::Completed ? CoreRecoveryReconcile::Completed :
                                                                 CoreRecoveryReconcile::Advanced;
    }
    error = "The installed version changed outside this unfinished installation. HQ retained its repair record and will not downgrade or switch destinations.";
    return CoreRecoveryReconcile::Conflict;
}

inline bool serializeCoreRecovery(const CoreRecoveryRecord& record, std::string& text,
                                  std::string& error) {
    if (!validateCoreRecovery(record, error)) return false;
    text = "HQCORE1\ntarget=" + installTargetKey(record.titleId, record.device) +
           "\nlanguage=" + record.language + "\nphase=" +
           (record.phase == CoreRecoveryPhase::BeforeDownload ? "download" :
            record.phase == CoreRecoveryPhase::Installing ? "installing" : "complete") +
           "\nstep=" + std::to_string(record.currentStep) + "\nprior=" + record.beforeVersion +
           "\nreplay=" + (record.replayFull ? "1" : "0") +
           "\ncount=" + std::to_string(record.plan.size()) + "\n";
    for (const auto& release : record.plan)
        text += "release=" + release.regionKey + "|" +
                (release.kind == ReleaseKind::Full ? "FULL" : "PATCH") + "|" +
                (release.kind == ReleaseKind::Full ? "-" : release.fromVersion) + "|" +
                release.version + "|" + release.downloadUrl + "|" + release.sha256 + "|" +
                std::to_string(release.compressedBytes) + "\n";
    return text.size() <= MaxCoreRecoveryBytes ||
           core_recovery_detail::fail(error, "The repair record is too large.");
}

inline bool parseCoreRecovery(const std::string& text, CoreRecoveryRecord& record,
                              std::string& error) {
    using namespace core_recovery_detail;
    record = {};
    error.clear();
    if (text.empty() || text.size() > MaxCoreRecoveryBytes)
        return fail(error, "The repair record is empty or exceeds its safe size limit.");
    std::string_view input(text);
    const auto line = [&input](std::string_view& output) {
        if (input.empty()) return false;
        const auto end = input.find('\n');
        output = input.substr(0, end);
        if (end == std::string_view::npos) input = {};
        else input.remove_prefix(end + 1);
        if (!output.empty() && output.back() == '\r') output.remove_suffix(1);
        return true;
    };
    std::string_view value;
    if (!line(value) || value != "HQCORE1") return fail(error, "Unknown repair record format.");
    const auto field = [&line, &value](std::string_view prefix) {
        if (!line(value) || value.rfind(prefix, 0) != 0) return false;
        value.remove_prefix(prefix.size());
        return true;
    };
    CoreRecoveryRecord parsed;
    if (!field("target=")) return fail(error, "Missing repair target.");
    InstallPreferences target;
    std::string targetError;
    if (!parseInstallPreferences("install_target=" + std::string(value) + "\n", target, targetError) ||
        target.updateDevice == Device::Missing) return fail(error, "Invalid repair target.");
    parsed.device = target.updateDevice;
    parsed.titleId = target.updateTitleId;
    if (!field("language=")) return fail(error, "Missing repair language.");
    parsed.language = value;
    if (!field("phase=")) return fail(error, "Missing repair phase.");
    if (value == "download") parsed.phase = CoreRecoveryPhase::BeforeDownload;
    else if (value == "installing") parsed.phase = CoreRecoveryPhase::Installing;
    else if (value == "complete") parsed.phase = CoreRecoveryPhase::Completed;
    else return fail(error, "Unknown repair phase.");
    std::uint64_t number = 0;
    if (!field("step=") || !decimal(value, number) || number > MaxCoreRecoverySteps)
        return fail(error, "Invalid repair package step.");
    parsed.currentStep = static_cast<std::size_t>(number);
    if (!field("prior=")) return fail(error, "Missing repair version marker.");
    parsed.beforeVersion = value;
    if (!field("replay=") || (value != "0" && value != "1")) return fail(error, "Invalid repair replay state.");
    parsed.replayFull = value == "1";
    if (!field("count=") || !decimal(value, number) || number == 0 || number > MaxCoreRecoverySteps)
        return fail(error, "Invalid repair package count.");
    const auto count = static_cast<std::size_t>(number);
    for (std::size_t index = 0; index < count; ++index) {
        if (!field("release=")) return fail(error, "Missing repair package metadata.");
        std::array<std::string_view, 7> parts{};
        for (std::size_t part = 0; part < parts.size(); ++part) {
            const auto separator = value.find('|');
            if ((separator == std::string_view::npos) != (part + 1 == parts.size()))
                return fail(error, "Invalid repair package field count.");
            parts[part] = value.substr(0, separator);
            if (separator == std::string_view::npos) value = {};
            else value.remove_prefix(separator + 1);
        }
        if ((parts[1] != "FULL" && parts[1] != "PATCH") || !decimal(parts[6], number))
            return fail(error, "Invalid repair package type or size.");
        const bool full = parts[1] == "FULL";
        if (full && parts[2] != "-") return fail(error, "Invalid full repair package prerequisite.");
        parsed.plan.push_back({std::string(parts[0]), full ? ReleaseKind::Full : ReleaseKind::Patch,
                               full ? std::string{} : std::string(parts[2]), std::string(parts[3]),
                               std::string(parts[4]), std::string(parts[5]), number});
    }
    if (!input.empty()) return fail(error, "Unexpected additional fields in the repair record.");
    if (!validateCoreRecovery(parsed, error)) return false;
    record = std::move(parsed);
    return true;
}

namespace core_recovery_detail {

enum class StateFile { Missing, Present, Invalid };

// Inject the three small file operations in tests. The real adapter checks
// bounded regular files and uses the standard SD text commit, never game I/O.
template<class Io>
CoreRecoveryLoadStatus loadStateWithIo(const std::string& path, CoreRecoveryRecord& record,
                                       std::string& error, bool allowRecoveryWrites, Io& io) {
    record = {};
    error.clear();
    CoreRecoveryRecord primary;
    const auto primaryStatus = io.read(path, primary, error);
    if (primaryStatus == StateFile::Invalid) return CoreRecoveryLoadStatus::Invalid;
    if (primaryStatus == StateFile::Present) {
        record = std::move(primary);
        return CoreRecoveryLoadStatus::Pending;
    }
    CoreRecoveryRecord previous;
    const auto old = path + ".unbound-old";
    const auto oldStatus = io.read(old, previous, error);
    if (oldStatus == StateFile::Invalid) return CoreRecoveryLoadStatus::Invalid;
    if (oldStatus == StateFile::Present) {
        if (allowRecoveryWrites && !io.rename(old, path, error)) return CoreRecoveryLoadStatus::Invalid;
        record = std::move(previous);
        return CoreRecoveryLoadStatus::Pending;
    }
    CoreRecoveryRecord staged;
    const auto temporary = path + ".unbound-new";
    const auto temporaryStatus = io.read(temporary, staged, error);
    if (temporaryStatus == StateFile::Invalid) return CoreRecoveryLoadStatus::Invalid;
    if (temporaryStatus == StateFile::Missing) return CoreRecoveryLoadStatus::Missing;
    if (staged.currentStep != 0 || staged.phase != CoreRecoveryPhase::BeforeDownload) {
        error = "Orphaned install repair metadata needs attention. HQ retained it instead of guessing.";
        return CoreRecoveryLoadStatus::Invalid;
    }
    if (allowRecoveryWrites && !io.rename(temporary, path, error)) return CoreRecoveryLoadStatus::Invalid;
    record = std::move(staged);
    return CoreRecoveryLoadStatus::Pending;
}

template<class Io>
bool removeExactStateWithIo(const std::string& path, const CoreRecoveryRecord& record,
                            std::string& error, Io& io) {
    if (!validateCoreRecovery(record, error)) return false;
    CoreRecoveryRecord saved;
    const auto loaded = loadStateWithIo(path, saved, error, false, io);
    if (loaded == CoreRecoveryLoadStatus::Invalid) return false;
    if (loaded == CoreRecoveryLoadStatus::Missing) return true;
    std::string expectedText, savedText;
    if (!serializeCoreRecovery(record, expectedText, error) ||
        !serializeCoreRecovery(saved, savedText, error)) return false;
    if (savedText != expectedText) {
        error = "The install repair record changed. It was not removed.";
        return false;
    }
    // Sidecars first: a power cut cannot resurrect Installing after the
    // authoritative completed primary has disappeared.
    return io.remove(path + ".unbound-new", error) &&
           io.remove(path + ".unbound-old", error) && io.remove(path, error);
}

} // namespace core_recovery_detail

} // namespace unbound
