#pragma once

#include "model.hpp"

namespace unbound::config {

#ifdef UNBOUND_DIAGNOSTIC_SD
inline constexpr bool DiagnosticSdBuild = true;
inline constexpr const char* AppName = "Black Ops 2 Unbound HQ - SD Test";
inline constexpr const char* AppVersion = "1.0.0-sd-test1";
#define UNBOUND_HQ_SD_ROOT "fs:/vol/external01/wiiu/apps/BO2-Unbound-HQ-SDTest"
#define UNBOUND_HQ_WUHB_NAME "BO2-Unbound-HQ-SDTest.wuhb"
#else
inline constexpr bool DiagnosticSdBuild = false;
inline constexpr const char* AppName = "Unbound HQ";
inline constexpr const char* AppVersion = "1.5";
#define UNBOUND_HQ_SD_ROOT "fs:/vol/external01/wiiu/apps/Unbound-HQ"
#define UNBOUND_HQ_WUHB_NAME "Unbound-HQ.wuhb"
#endif
// Monotonic integer used only by the HQ self-updater. Increment it for every
// published WUHB, even when the display version is a beta or hotfix label.
inline constexpr unsigned long long AppReleaseRevision = 66;

// Replace YOUR_GITHUB_NAME and the repository name with your public distribution repository.
// See server-example/README.md for the recommended GitHub repository and Release layout.
inline constexpr const char* ReleasesUrl =
    "https://downloads.unc-bo2.fun/release-manifest.txt";
inline constexpr const char* MapPacksUrl = "https://downloads.unc-bo2.fun/release-manifest.txt";
inline constexpr const char* SdContentRoot = "fs:/vol/external01/unbound/content";
inline constexpr const char* ChangesUrl =
    "https://downloads.unc-bo2.fun/recent-changes.txt";
inline constexpr const char* WorkshopUrl =
    "https://raw.githubusercontent.com/tonytrawl/bo2-unbound/main/workshop-plugins.txt";
inline constexpr const char* ProtariumReleaseUrl =
    "https://downloads.unc-bo2.fun/release-manifest.txt";
inline constexpr const char* GamePadMicReleaseUrl =
    "https://downloads.unc-bo2.fun/release-manifest.txt";

inline constexpr const char* SdRoot =
    UNBOUND_HQ_SD_ROOT;
inline constexpr const char* CacheRoot =
    UNBOUND_HQ_SD_ROOT "/cache";
inline constexpr const char* TesterArchivePath = "fs:/vol/external01/unbound/tester.zip";
inline constexpr const char* SupplyRunSettingsPath = UNBOUND_HQ_SD_ROOT "/supply-run.txt";
inline constexpr const char* SdModsRoot =
    "fs:/vol/external01/unbound/content/mods";
inline constexpr const char* ModLinkDisableFlagPath =
    "fs:/vol/external01/unbound/disable_redirect.flag";
// r10 briefly used this marker for an Aroma-side content update toast. r11
// removes it during startup so an already-loaded older plugin cannot keep
// announcing a stale result while the user transitions to Mod Link 0.2.1.
inline constexpr const char* LegacyUpdateAvailableFlagPath =
    "fs:/vol/external01/unbound/update_available.flag";
inline constexpr const char* SettingsPath =
    UNBOUND_HQ_SD_ROOT "/settings.ini";
inline constexpr const char* LastInstallFailurePath =
    UNBOUND_HQ_SD_ROOT "/last-install-failure.txt";
inline constexpr const char* ModSyncStateRoot =
    UNBOUND_HQ_SD_ROOT "/state";
inline constexpr const char* PluginStateRoot =
    UNBOUND_HQ_SD_ROOT "/state/plugins";
inline constexpr const char* PluginCacheRoot =
    UNBOUND_HQ_SD_ROOT "/cache/plugins";
inline constexpr const char* WorkshopCachePath =
    UNBOUND_HQ_SD_ROOT "/cache/workshop-plugins.txt";
inline constexpr const char* AromaRoot =
    "fs:/vol/external01/wiiu/environments/aroma";
inline constexpr const char* ChangesCachePath =
    UNBOUND_HQ_SD_ROOT "/cache/recent-changes.txt";
inline constexpr const char* ReleasesCachePath =
    UNBOUND_HQ_SD_ROOT "/cache/releases-chain-v2.txt";
inline constexpr const char* LegacyChangesCachePath =
    "fs:/vol/external01/wiiu/apps/bo2-unbound-workshop/cache/recent-changes.txt";
inline constexpr const char* PublishedSdRoot = "fs:/vol/external01/wiiu/apps/Unbound-HQ";
inline constexpr const char* LegacyHqSdRoot = "fs:/vol/external01/wiiu/apps/BO2-Unbound-HQ";
inline constexpr const char* AppWuhbName = UNBOUND_HQ_WUHB_NAME;
inline constexpr const char* AppWuhbPath =
    UNBOUND_HQ_SD_ROOT "/" UNBOUND_HQ_WUHB_NAME;
inline constexpr const char* AppUpdateStagingPath =
    UNBOUND_HQ_SD_ROOT "/" UNBOUND_HQ_WUHB_NAME ".unbound-new";
inline constexpr const char* AppBackupPath =
    UNBOUND_HQ_SD_ROOT "/" UNBOUND_HQ_WUHB_NAME ".unbound-old";

// The HQ binary and Aroma plugin state are shared. Game content and install
// records are isolated. Only the main menu changes mode, between operations.
inline Game activeGame = Game::BlackOpsII;
inline bool ghostsMode() { return activeGame == Game::Ghosts; }
inline constexpr const char* GhostsReleasesUrl = "https://downloads.unc-bo2.fun/ghosts/release-manifest.txt";
inline constexpr const char* GhostsChangesUrl = "https://downloads.unc-bo2.fun/ghosts/recent-changes.txt";
inline const char* gameReleasesUrl() { return ghostsMode() ? GhostsReleasesUrl : ReleasesUrl; }
inline const char* gameChangesUrl() { return ghostsMode() ? GhostsChangesUrl : ChangesUrl; }
inline const char* gameCustomMapsUrl() {
    return ghostsMode()?"https://downloads.unc-bo2.fun/ghosts/custom-maps.txt":
                        "https://downloads.unc-bo2.fun/custom-maps.txt";
}
inline const char* gameModsUrl() {
    return ghostsMode()?"https://downloads.unc-bo2.fun/ghosts/mods.txt":
                        "https://downloads.unc-bo2.fun/mods.txt";
}
inline const char* gameReleasesCachePath() {
    return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/cache/release-manifest.txt" : ReleasesCachePath;
}
inline const char* gameChangesCachePath() {
    return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/cache/recent-changes.txt" : ChangesCachePath;
}
inline const char* gameCacheRoot() { return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/cache" : CacheRoot; }
inline const char* gameStateRoot() { return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/state" : ModSyncStateRoot; }
inline const char* gameInstallSettingsPath() {
    return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/settings.ini" : SettingsPath;
}
inline const char* gameInstallFailurePath() {
    return ghostsMode() ? UNBOUND_HQ_SD_ROOT "/ghosts/last-install-failure.txt" : LastInstallFailurePath;
}
inline const char* gameSdContentRoot() {
    return ghostsMode() ? "fs:/vol/external01/unbound-ghosts/content" : SdContentRoot;
}
inline const char* gameSdModsRoot() {
    return ghostsMode() ? "fs:/vol/external01/unbound-ghosts/content/mods" : SdModsRoot;
}
inline const char* gameSdContentDisplayPath() {
    return ghostsMode() ? "sd:/unbound-ghosts/content" : "sd:/unbound/content";
}
inline const char* gameSdModsDisplayPath() {
    return ghostsMode() ? "sd:/unbound-ghosts/content/mods" : "sd:/unbound/content/mods";
}

#undef UNBOUND_HQ_SD_ROOT
#undef UNBOUND_HQ_WUHB_NAME

// The core installer writes this only after every archive entry succeeds.
inline constexpr const char* VersionRelativePath = "content/update.txt";

inline constexpr unsigned MaxManifestBytes = 512 * 1024;
inline constexpr unsigned MaxSettingsBytes = 16 * 1024;
inline constexpr unsigned MaxModSyncLedgerBytes = 128 * 1024;
inline constexpr unsigned MaxNewsImageBytes = 8 * 1024 * 1024;
inline constexpr unsigned MaxPluginFiles = 16;
inline constexpr unsigned long long MaxPluginArchiveBytes = 32ULL * 1024 * 1024;
inline constexpr unsigned long long MaxPluginExpandedBytes = 64ULL * 1024 * 1024;
inline constexpr unsigned long long MaxAppUpdateBytes = 64ULL * 1024 * 1024;
inline constexpr unsigned MaxArchiveFiles = 4096;
// FAT32 permits a single ZIP up to 4 GiB minus one byte. Keep individual
// payload files and the expanded total under their independent limits.
inline constexpr unsigned long long MaxArchiveBytes = 4ULL * 1024 * 1024 * 1024 - 1;
inline constexpr unsigned long long MaxArchiveEntryBytes = 2ULL * 1024 * 1024 * 1024;
inline constexpr unsigned long long MaxArchiveExpandedBytes = 5ULL * 1024 * 1024 * 1024;

} // namespace unbound::config
