#pragma once

#include "config.hpp"
#include "model.hpp"
#include "download_control.hpp"
#include "package_layout.hpp"

#include <functional>
#include <string>

namespace unbound {

using ProgressCallback = std::function<void(const std::string&, const std::string&, int)>;

bool initializePlatform(std::string& error, const ProgressCallback& progress = {});
void shutdownPlatform();
void setOperationFrameCallback(std::function<void()> callback);
enum class AppPathMigration { Unchanged, Migrated, Failed };
AppPathMigration migrateApplicationPath(std::string& error);

struct GameLaunchTarget {
    std::uint64_t titleId = 0;
    Device device = Device::Missing;
    // A validated Wii U Menu launch path. An empty path launches by title ID.
    std::string path;
};
GameLaunchTarget prepareGameLaunch(const GameInstall& install);
bool detectInstallation(GameInstall& install, std::string& error,
                        const ProgressCallback& progress = {},
                        bool allowRecoveryWrites = true);
bool loadInstallPreferences(InstallPreferences& preferences, std::string& warning,
                            bool allowRecoveryWrites = true);
bool saveInstallPreferences(const InstallPreferences& preferences, std::string& error);
// A non-empty error with a successful scan is a warning about a device or
// invalid title folder; valid choices from other storage remain available.
bool scanInstallOptions(std::vector<InstallOption>& options, std::string& error,
                        const ProgressCallback& progress = {});
bool applyInstallPreferences(GameInstall& install, const InstallPreferences& preferences,
                             std::string& error, const ProgressCallback& progress = {},
                             bool allowRecoveryWrites = true);
bool readTextFile(const std::string& path, std::string& output, std::size_t maximum = 1024 * 1024);
bool writeTextFileAtomic(const std::string& path, const std::string& value, std::string& error);
bool ensureDirectory(const std::string& path);
bool downloadText(const std::string& url, std::string& output, std::string& error);
// A nonzero release-record size skips HEAD, requires an expected SHA-256 and
// is checked exactly before commit. Zero keeps the unknown-size probe path.
bool downloadFile(const std::string& url, const std::string& destination, std::string& error,
                  const ProgressCallback& progress = {},
                  unsigned long long maximumBytes = config::MaxArchiveBytes,
                  const std::string* expectedSha256 = nullptr,
                  DownloadControl* control = nullptr,
                  unsigned long long expectedBytes = 0);
bool installApplicationUpdate(const std::string& stagedWuhb, std::string& error);
bool verifyDownloadedPackage(const std::string& archive, const std::string& expected, std::string& error);
enum class PackageVerificationStatus { Valid, Cancelled, InvalidContent, ReadFailure, MetadataMismatch };
PackageVerificationStatus checkDownloadedPackage(const std::string& archive,
    const std::string& expected, unsigned long long expectedBytes, std::string& error,
    const ProgressCallback& progress = {}, DownloadControl* control = nullptr);
// Clears only this core package's hash-named ZIP and resume files. The install
// record, game files and version marker are never changed by a cache reset.
bool resetCorePackageDownloadCache(const std::string& sha256, std::string& error);
struct InheritedContentRepairInfo {
    bool needed = false;
    unsigned long long copyBytes = 0;
    unsigned long long requiredBytes = 0;
    unsigned long long freeBytes = 0;
    unsigned folders = 0;
    unsigned files = 0;
    // Converted shared movie folder whose retained reference HQ puts back.
    unsigned restoreFolders = 0;
    unsigned long long reclaimBytes = 0;
    std::string summary;
};
// Read-only discovery includes owned r35/r36 directory conversions even when
// update.txt is already current. It never relies on a downloaded game ZIP.
bool inspectInheritedContentRepair(const GameInstall& install, InheritedContentRepairInfo& info,
                                   std::string& error, const ProgressCallback& progress = {});
// Read-only FSA stat dump of native link entries for FSMakeLink calibration.
// Written to native-link-census.txt in the HQ SD folder; never writes the title.
bool writeNativeLinkCensus(const GameInstall& install, std::string& error);
struct SupportExportResult {std::string path,warning;unsigned consoleLogs=0,otherLogs=0;};
bool exportSupportArchive(const GameInstall& install,const std::string& report,
                          SupportExportResult& result,std::string& error,
                          const ProgressCallback& progress = {});
bool repairInheritedContent(const GameInstall& install, std::string& error,
                             const ProgressCallback& progress = {});
bool installCoreArchive(const std::string& archive, const GameInstall& install,
                        const std::string& newVersion, std::string& error,
                        const ProgressCallback& progress = {},
                        const std::function<bool(std::string&)>& beforeMutation = {});
bool readInstallState(const std::string& path, std::string& text, std::string& error);
bool retireModLink(bool& changed, std::string& error);
bool installMapArchive(const std::string& archive, const GameInstall& install,
                       const std::string& id, const std::string& version,
                       std::string& error, const ProgressCallback& progress = {},
                       const std::function<bool(std::string&)>& beforeMutation = {});
bool installTesterArchive(const GameInstall& install, std::string& error,
                          const ProgressCallback& progress = {});
bool listSdPackages(const std::string& directory,std::vector<SdPackageEntry>& entries,std::string& error);
bool inspectLocalPackage(const std::string& archive,const GameInstall& install,LocalPackageInfo& info,
                         std::string& error,const ProgressCallback& progress = {});
bool installLocalPackage(const LocalPackageInfo& info,const GameInstall& install,std::string& error,
                         const ProgressCallback& progress = {},
                         const std::function<bool(std::string&)>& beforeMutation = {});
#ifdef UNBOUND_DIAGNOSTIC_SD
// Local-only diagnostic entry point. The digest binds the confirmed ZIP to the
// file that is installed; it is not publisher authentication or a release tag.
struct DiagnosticArchiveInfo {
    bool present = false;
    unsigned long long compressedBytes = 0;
    std::string sha256;
};
const char* diagnosticArchivePath();
const char* diagnosticInstalledVersion();
bool inspectDiagnosticArchive(DiagnosticArchiveInfo& info, std::string& error,
                              const ProgressCallback& progress = {});
bool installDiagnosticArchive(const DiagnosticArchiveInfo& info,
                              const GameInstall& install, std::string& error,
                              const ProgressCallback& progress = {});
#endif
bool installWorkshopArchive(const std::string& archive, const GameInstall& install,
                            const WorkshopMod& mod, std::string& error,
                            const ProgressCallback& progress = {});
bool scanLocalMods(const GameInstall& install, bool installed, std::vector<LocalMod>& mods,
                   std::string& error, bool allowRecoveryWrites = true);
bool scanLocalModInventory(const GameInstall& install,
                           std::vector<LocalModInventoryItem>& inventory,
                           std::string& error, bool allowRecoveryWrites = true);

struct LegacyModMigrationInfo {
    unsigned folderCount = 0;
    unsigned otherEntryCount = 0;
    std::vector<std::string> roots;
    std::vector<std::string> folders;
};

struct LegacyModMigrationResult {
    unsigned moved = 0;
    unsigned conflicts = 0;
    unsigned skipped = 0;
    std::vector<std::string> conflictFolders;
    std::vector<std::string> rootsStillPresent;
};

bool inspectLegacySdMods(LegacyModMigrationInfo& info, std::string& error);
bool migrateLegacySdMods(LegacyModMigrationResult& result, std::string& error);
bool loadModLinkSetting(bool& enabled, std::string& warning,
                        bool allowRecoveryWrites = true);
bool saveModLinkSetting(bool enabled, std::string& error);
bool deleteSdMod(const LocalMod& mod, std::string& error,
                 const ProgressCallback& progress = {});
bool loadSdAutoSyncSetting(bool& enabled, std::string& warning,
                           bool allowRecoveryWrites = true);
bool saveSdAutoSyncSetting(bool enabled, std::string& error);
bool loadPluginAutoUpdateSetting(const std::string& pluginKey, bool& enabled,
                                 std::string& warning,
                                 bool allowRecoveryWrites = true);
bool savePluginAutoUpdateSetting(const std::string& pluginKey, bool enabled,
                                 std::string& error);
bool inspectPluginInstall(const WorkshopPlugin& plugin, const PluginRelease* latest,
                          PluginInstallStatus& status, std::string& warning,
                          bool allowRecoveryWrites = true);
bool installPluginArchive(const std::string& archive, const WorkshopPlugin& plugin,
                          const PluginRelease& release, bool replaceUnmanaged,
                          std::string& error, const ProgressCallback& progress = {});
bool synchronizeSdMods(const GameInstall& install, SdModSyncResult& result,
                       std::string& error, const ProgressCallback& progress = {});
bool sdModRequiresOwnershipAdoption(const GameInstall& install, const LocalMod& mod,
                                    bool& required, std::string& error);
bool copySdModToGame(const GameInstall& install, const LocalMod& mod, std::string& error,
                     bool adoptUnmanagedFolder = false,
                     const ProgressCallback& progress = {});
bool copyInstalledModToSd(const GameInstall& install, const LocalMod& mod,
                          std::string& error, bool replaceExisting = false,
                          const ProgressCallback& progress = {});
bool deleteInstalledMod(const GameInstall& install, const LocalMod& mod, std::string& error,
                        const ProgressCallback& progress = {});
bool cleanupDownloadCache(const std::vector<std::string>& activeNewsImages,
                          unsigned long long& bytesFreed, std::string& error);

} // namespace unbound
