#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace unbound {

enum class Game { BlackOpsII, Ghosts };

const char* gameName(Game game);

enum class Region { Unknown, USA, Europe, Japan };
enum class Device { Missing, MLC, USB, Disc };

struct GameSku {
    const char* key;
    const char* label;
    Region region;
    std::uint64_t baseTitleId;
    std::uint64_t updateTitleId;
    std::uint64_t aocTitleId;
    Game game = Game::BlackOpsII;
};

struct InstallLocation {
    Device device = Device::Missing;
    std::string root;
    std::uint16_t titleVersion = 0;
    bool present = false;
};

struct GameInstall {
    const GameSku* sku = nullptr;
    Region consoleRegion = Region::Unknown;
    InstallLocation base;
    InstallLocation update;
    InstallLocation aoc;
    std::string modVersion;
    std::string languageFolder = "english";
    bool targetOverridden = false;
    bool languageOverridden = false;
    bool destinationReady = true;
    std::string destinationWarning;
    Game game = Game::BlackOpsII;
};

// Only existing, validated Black Ops II update roots can be selected. Missing
// device/title and an empty language mean Auto, not an arbitrary filesystem path.
struct InstallPreferences {
    Device updateDevice = Device::Missing;
    std::uint64_t updateTitleId = 0;
    std::string languageFolder;
    // A language change needs the complete package chain even when the shared
    // update.txt marker is current. Retain this until the entire chain succeeds.
    bool languageInstallPending = false;
};

struct InstallOption {
    GameInstall install;
    std::vector<std::string> languages;
};

// The requirement follows the detected game, not the console's region. A
// present update with version zero has not been identified reliably.
enum class OfficialUpdateStatus { UnknownGame, Missing, UnknownVersion, Outdated, Current };

enum class ReleaseKind { Full, Patch };

struct Release {
    std::string regionKey;
    ReleaseKind kind = ReleaseKind::Full;
    std::string fromVersion;
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t compressedBytes = 0;
};

struct AppRelease {
    std::uint64_t revision = 0;
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t fileBytes = 0;
};

struct WorkshopMod {
    std::string id;
    std::string name;
    std::string creator;
    std::string description;
    std::string downloadUrl;
    std::string sha256;
    unsigned upvotes = 0;
    unsigned downvotes = 0;
    unsigned commentCount = 0;
    std::string featuredComment;
};

struct WorkshopPlugin {
    std::string key;
    std::string name;
    std::string creator;
    std::string description;
    std::string releaseFeedUrl;
    std::string homepageUrl;
    // Exact ZIP paths approved by the Unbound-owned catalog. The developer's
    // release feed cannot expand this write allowlist.
    std::vector<std::string> managedPaths;
};

struct PluginFile {
    std::string archivePath;
    std::string sha256;
    std::uint64_t bytes = 0;
};

struct PluginRelease {
    std::string pluginKey;
    std::uint64_t revision = 0;
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t compressedBytes = 0;
    std::vector<PluginFile> files;
};

enum class PluginPresence { NotInstalled, Managed, Unmanaged, Damaged };

struct PluginInstallStatus {
    PluginPresence presence = PluginPresence::NotInstalled;
    std::uint64_t installedRevision = 0;
    std::string installedVersion;
    bool updateAvailable = false;
    // True only when a valid HQ receipt exists and reinstalling the trusted
    // release can safely replace files whose hashes no longer match it.
    bool repairable = false;
};

struct Review {
    std::string author;
    std::string body;
};

struct LocalMod {
    std::string folder;
    std::string name;
    std::string description;
    std::string author;
    // Empty means a legacy manifest and deliberately forces a fresh copy on
    // every SD Auto-Sync run. Non-empty tokens are namespaced by their key.
    std::string syncToken;
    bool installed = false;
};

// One logical mod folder as presented by the Mod Manager. A folder may exist
// on the SD card, in the installed update, or in both locations.
struct LocalModInventoryItem {
    std::string folder;
    LocalMod sd;
    LocalMod console;
    bool hasSd = false;
    bool hasConsole = false;
};

struct SdModSyncResult {
    unsigned copied = 0;
    unsigned updated = 0;
    unsigned unchanged = 0;
    unsigned conflicts = 0;
    std::vector<std::string> conflictFolders;
};

struct NewsImage {
    std::string caption;
    std::string url;
    std::string cachePath;
};

inline constexpr GameSku SupportedSkus[] = {
    {"USA", "USA", Region::USA, 0x000500001010CF00ULL,
     0x0005000E1010CF00ULL, 0x0005000C1010CF00ULL},
    {"EUR_EN_FR", "Europe (English/French)", Region::Europe,
     0x0005000010113400ULL, 0x0005000E10113400ULL, 0x0005000C10113400ULL},
    {"EUR_ES_IT", "Europe (Spanish/Italian)", Region::Europe,
     0x0005000010113500ULL, 0x0005000E10113500ULL, 0x0005000C10113500ULL},
    {"EUR_DE", "Europe (German)", Region::Europe,
     0x0005000010113700ULL, 0x0005000E10113700ULL, 0x0005000C10113700ULL},
    {"JPN", "Japan", Region::Japan, 0x000500001011B400ULL,
     0x0005000E1011B400ULL, 0x0005000C1011B400ULL},
};

inline constexpr GameSku GhostsSkus[] = {
    {"USA", "USA", Region::USA, 0x0005000010146800ULL,
     0x0005000E10146800ULL, 0x0005000C10146800ULL, Game::Ghosts},
    {"EUR_EN_FR_DE", "Europe (English/French/German)", Region::Europe,
     0x0005000010156100ULL, 0x0005000E10156100ULL, 0x0005000C10156100ULL, Game::Ghosts},
    {"EUR_ES_IT", "Europe (Spanish/Italian)", Region::Europe,
     0x0005000010157600ULL, 0x0005000E10157600ULL, 0x0005000C10157600ULL, Game::Ghosts},
    {"JPN", "Japan", Region::Japan, 0x0005000010157900ULL,
     0x0005000E10157900ULL, 0x0005000C10157900ULL, Game::Ghosts},
};

inline std::span<const GameSku> gameSkus(Game game) {
    if (game == Game::Ghosts) return GhostsSkus;
    return SupportedSkus;
}

const char* regionName(Region region);
const char* deviceName(Device device);
const GameSku* findSkuByTitle(std::uint64_t titleId);
std::uint16_t requiredOfficialUpdateVersion(const GameSku* sku);
OfficialUpdateStatus officialUpdateStatus(const GameInstall& install);
std::string officialUpdateSummary(const GameInstall& install);
// Empty when current; all other states are informational warnings, never an
// authorization to prevent an Unbound installation.
std::string officialUpdateWarning(const GameInstall& install);
std::string languageFolderForSku(const GameSku* sku, const std::string& locale);
bool supportedLanguageFolder(const std::string& folder);
bool validInstallPreferences(const InstallPreferences& preferences);
std::string installTargetKey(std::uint64_t updateTitleId, Device device);
bool parseInstallPreferences(const std::string& text, InstallPreferences& preferences,
                             std::string& error);
std::string replaceInstallPreferences(const std::string& text,
                                      const InstallPreferences& preferences);
bool localizeArchivePath(const std::string& path, const std::string& languageFolder,
                         std::string& localized);
std::string trim(std::string value);
std::vector<std::string> splitEscaped(const std::string& line, char separator);
bool validId(const std::string& id);
bool safeRelativePath(const std::string& path);
std::vector<std::string> directoryPrefixes(const std::string& path);
int compareVersions(const std::string& left, const std::string& right);
bool releaseAppliesToInstalledVersion(const Release& release,
                                      const std::string& installedVersion);
std::vector<Release> parseReleases(const std::string& text, std::vector<std::string>* errors = nullptr);
std::vector<AppRelease> parseAppReleases(const std::string& text,
                                         std::vector<std::string>* errors = nullptr);
const AppRelease* latestAppRelease(const std::vector<AppRelease>& releases);
bool sameAppRelease(const AppRelease& left, const AppRelease& right);
bool replaceAppReleaseDirectives(const std::string& text,
                                 const AppRelease* retainedRelease,
                                 std::size_t maximumBytes,
                                 std::string& output);
std::vector<WorkshopMod> parseWorkshop(const std::string& text, std::vector<std::string>* errors = nullptr);
std::vector<WorkshopPlugin> parsePluginCatalog(const std::string& text,
                                                std::vector<std::string>* errors = nullptr);
bool parsePluginRelease(const std::string& text, const WorkshopPlugin& plugin,
                        PluginRelease& release, std::vector<std::string>* errors = nullptr);
bool samePluginRelease(const PluginRelease& left, const PluginRelease& right);
const Release* releaseForSku(const std::vector<Release>& releases, const GameSku* sku);
std::string latestReleaseVersion(const std::vector<Release>& releases, const GameSku* sku);
std::vector<const Release*> releasePlanForSku(const std::vector<Release>& releases,
                                               const GameSku* sku,
                                               const std::string& installedVersion);
int applyWorkshopVote(WorkshopMod& mod, int currentVote, int requestedVote);
LocalMod parseModload(const std::string& text, const std::string& folder, bool installed);
std::string modSyncTokenFromManifest(const std::string& text);
std::vector<NewsImage> parseNewsImages(const std::string& text);
std::string plainNewsPreview(const std::string& text);
double updateSmoothedRate(double previousUnitsPerSecond, std::uint64_t completedDelta,
                          std::uint64_t elapsedMilliseconds);
bool estimateRemainingSeconds(std::uint64_t completed, std::uint64_t total,
                              double unitsPerSecond, std::uint64_t& seconds);
std::string formatRemainingTime(std::uint64_t seconds);

} // namespace unbound
