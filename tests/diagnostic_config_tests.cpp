#include "config.hpp"

#include <cassert>
#include <iostream>
#include <string_view>

int main() {
    using namespace unbound::config;
    using namespace std::literals;
    assert(AppReleaseRevision == 66);
    assert(ReleasesUrl == "https://downloads.unc-bo2.fun/release-manifest.txt"sv);
    assert(std::string_view(MapPacksUrl)==ReleasesUrl);
    assert(VersionRelativePath == "content/update.txt"sv);
    assert(std::string_view(gameReleasesUrl()) == ReleasesUrl);
    assert(std::string_view(gameInstallSettingsPath()) == SettingsPath);
    assert(std::string_view(gameStateRoot()) == ModSyncStateRoot);
    activeGame = unbound::Game::Ghosts;
    assert(std::string_view(gameReleasesUrl()) == GhostsReleasesUrl);
    assert(std::string_view(gameChangesUrl()) == GhostsChangesUrl);
    assert(std::string_view(gameInstallSettingsPath()).ends_with("/ghosts/settings.ini"));
    assert(std::string_view(gameStateRoot()).ends_with("/ghosts/state"));
    assert(std::string_view(gameCacheRoot()).ends_with("/ghosts/cache"));
    assert(std::string_view(gameSdContentRoot()) == "fs:/vol/external01/unbound-ghosts/content");
    assert(std::string_view(gameSdContentDisplayPath()) == "sd:/unbound-ghosts/content");
    assert(std::string_view(gameSdModsRoot()).ends_with("/unbound-ghosts/content/mods"));
    activeGame = unbound::Game::BlackOpsII;
    assert(std::string_view(gameChangesUrl()) == ChangesUrl);
#ifdef UNBOUND_DIAGNOSTIC_SD
    static_assert(DiagnosticSdBuild);
    assert(AppVersion == "1.0.0-sd-test1"sv);
    assert(SdRoot == "fs:/vol/external01/wiiu/apps/BO2-Unbound-HQ-SDTest"sv);
    assert(SettingsPath == "fs:/vol/external01/wiiu/apps/BO2-Unbound-HQ-SDTest/settings.ini"sv);
    assert(AppWuhbPath == "fs:/vol/external01/wiiu/apps/BO2-Unbound-HQ-SDTest/BO2-Unbound-HQ-SDTest.wuhb"sv);
    assert(std::string_view(CacheRoot).starts_with(SdRoot));
    assert(std::string_view(ModSyncStateRoot).starts_with(SdRoot));
    assert(std::string_view(PluginStateRoot).starts_with(SdRoot));
    assert(std::string_view(ReleasesCachePath).starts_with(SdRoot));
    std::cout << "Diagnostic application paths and identity are isolated\n";
#else
    static_assert(!DiagnosticSdBuild);
    assert(AppName == "Unbound HQ"sv);
    assert(AppVersion == "1.5"sv);
    assert(SdRoot == "fs:/vol/external01/wiiu/apps/Unbound-HQ"sv);
    assert(SettingsPath == "fs:/vol/external01/wiiu/apps/Unbound-HQ/settings.ini"sv);
    assert(CacheRoot == "fs:/vol/external01/wiiu/apps/Unbound-HQ/cache"sv);
    assert(AppWuhbPath == "fs:/vol/external01/wiiu/apps/Unbound-HQ/Unbound-HQ.wuhb"sv);
    std::cout << "Regular HQ configuration keeps its established install path\n";
#endif
}

