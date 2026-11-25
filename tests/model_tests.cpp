#include "model.hpp"
#include "sha256.hpp"

#include <cassert>
#include <iostream>
#include <limits>

int main() {
    using namespace unbound;
    assert(findSkuByTitle(0x0005000E1010CF00ULL));
    assert(findSkuByTitle(0x0005000010113500ULL)->region == Region::Europe);
    assert(languageFolderForSku(findSkuByTitle(0x0005000010113400ULL), "fr-FR") == "french");
    assert(languageFolderForSku(findSkuByTitle(0x0005000010113500ULL), "it-IT") == "italian");
    assert(languageFolderForSku(findSkuByTitle(0x0005000010113700ULL), "en-GB") == "german");
    assert(languageFolderForSku(findSkuByTitle(0x000500001011B400ULL), "en-US") == "japanese");
    std::string localized;
    assert(localizeArchivePath("update/content/@language/patch.ff", "spanish", localized));
    assert(localized == "update/content/spanish/patch.ff");
    assert(localizeArchivePath("update/content/0010/@language/map.ff", "french", localized));
    assert(localized == "update/content/0010/french/map.ff");
    assert(!localizeArchivePath("update/content/@language/patch.ff", "../../bad", localized));
    assert(std::string(deviceName(Device::Disc)) == "Game disc");
    GameInstall official;
    assert(requiredOfficialUpdateVersion(nullptr) == 0);
    assert(officialUpdateStatus(official) == OfficialUpdateStatus::UnknownGame);
    assert(!officialUpdateWarning(official).empty());
    for (const auto& sku : SupportedSkus) {
        official.sku = &sku;
        // Imported games follow their own SKU even on a different-region console.
        official.consoleRegion = sku.region == Region::Japan ? Region::USA : Region::Japan;
        const std::uint16_t required = sku.region == Region::Japan ? 96 : 128;
        assert(requiredOfficialUpdateVersion(&sku) == required);
        official.update.present = false;
        official.update.titleVersion = required;
        assert(officialUpdateStatus(official) == OfficialUpdateStatus::Missing);
        assert(!officialUpdateWarning(official).empty());
        official.update.present = true;
        official.update.titleVersion = 0;
        assert(officialUpdateStatus(official) == OfficialUpdateStatus::UnknownVersion);
        assert(!officialUpdateWarning(official).empty());
        official.update.titleVersion = required - 1;
        assert(officialUpdateStatus(official) == OfficialUpdateStatus::Outdated);
        assert(officialUpdateWarning(official).find("v" + std::to_string(required)) != std::string::npos);
        official.update.titleVersion = required;
        assert(officialUpdateStatus(official) == OfficialUpdateStatus::Current);
        assert(officialUpdateWarning(official).empty());
        assert(officialUpdateSummary(official) == "v" + std::to_string(required) + " - current");
        official.update.titleVersion = required + 1;
        assert(officialUpdateStatus(official) == OfficialUpdateStatus::Current);
        assert(officialUpdateWarning(official).empty());
        assert(officialUpdateSummary(official).find(std::to_string(required + 1)) != std::string::npos);
    }
    const GameSku unidentified{"UNKNOWN", "Unknown game", Region::Unknown, 1, 2, 3};
    official.sku = &unidentified;
    assert(requiredOfficialUpdateVersion(official.sku) == 0);
    assert(officialUpdateStatus(official) == OfficialUpdateStatus::UnknownGame);
    assert(compareVersions("1.9.0", "1.10.0") < 0);
    assert(compareVersions("2.0", "2.0.0") == 0);
    assert(compareVersions("0.0.05", "0.0.05P1.0") < 0);
    assert(compareVersions("0.0.05P1.0", "0.0.05P2.0") < 0);
    assert(compareVersions("0.0.05P99.0", "0.0.06") < 0);
    assert(validId("zm-pack_2"));
    assert(!validId("../bad"));
    assert(safeRelativePath("maps/zm/file.ff"));
    assert(!safeRelativePath("../code/game.rpx"));
    assert(!safeRelativePath("C:/bad"));
    assert(!safeRelativePath(std::string("maps/\0oops", 10)));
    auto dirs = directoryPrefixes("fs:/vol/external01/wiiu/apps/bo2-unbound-workshop/cache");
    assert(dirs.size() == 4);
    assert(dirs.front() == "fs:/vol/external01/wiiu");
    assert(dirs.back() == "fs:/vol/external01/wiiu/apps/bo2-unbound-workshop/cache");
    for (const auto& dir : dirs) {
        assert(dir != "fs:/vol");
        assert(dir != "fs:/vol/external01");
    }

    std::vector<std::string> errors;
    auto releases = parseReleases("# comment\nUSA|1.2.0|https://example.test/u.zip|abc\n", &errors);
    assert(errors.empty() && releases.size() == 1 && releases[0].version == "1.2.0");
    errors.clear();
    releases = parseReleases("USA|0.0.01|https://example.test/update.zip|abc|https://example.test/aoc.zip|def\n", &errors);
    assert(releases.empty() && errors.size() == 1);
    errors.clear();
    releases = parseReleases("UNIVERSAL|0.0.02|https://example.test/update.zip|abc\r\n", &errors);
    assert(errors.empty() && releaseForSku(releases, findSkuByTitle(0x0005000010113500ULL)) == &releases[0]);
    errors.clear();
    const std::string digest(64, 'a');
    releases = parseReleases(
        "# PACKAGE_KEY|KIND|FROM_VERSION|TO_VERSION|DOWNLOAD_URL|SHA256|COMPRESSED_BYTES\n"
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P1.0|https://example.test/patch.zip|" + digest + "|8000000\n",
        &errors);
    assert(errors.empty() && releases.size() == 2);
    assert(releases[0].kind == ReleaseKind::Full && releases[0].compressedBytes == 200000000);
    assert(releases[1].kind == ReleaseKind::Patch && releases[1].fromVersion == "0.0.05");
    assert(releaseAppliesToInstalledVersion(releases[0], "Not installed"));
    assert(!releaseAppliesToInstalledVersion(releases[0], "0.0.05"));
    assert(releaseAppliesToInstalledVersion(releases[1], "0.0.05"));
    assert(!releaseAppliesToInstalledVersion(releases[1], "0.0.04P9.0"));
    assert(!releaseAppliesToInstalledVersion(releases[1], "0.0.05P1.0"));
    const auto* europeanSku = findSkuByTitle(0x0005000010113500ULL);
    auto plan = releasePlanForSku(releases, europeanSku, "Not installed");
    assert(plan.size() == 2 && plan[0]->kind == ReleaseKind::Full &&
           plan[1]->kind == ReleaseKind::Patch && plan[1]->version == "0.0.05P1.0");
    plan = releasePlanForSku(releases, europeanSku, "0.0.05");
    assert(plan.size() == 1 && plan[0]->kind == ReleaseKind::Patch);
    assert(releasePlanForSku(releases, europeanSku, "0.0.05P1.0").empty());
    plan = releasePlanForSku(releases, europeanSku, "0.0.04");
    assert(plan.size() == 2 && plan[0]->kind == ReleaseKind::Full &&
           plan[1]->kind == ReleaseKind::Patch);
    assert(latestReleaseVersion(releases, europeanSku) == "0.0.05P1.0");
    errors.clear();
    const auto shortcuts = parseReleases(
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P1.0|https://example.test/p1.zip|" + digest + "|100\n"
        "UNIVERSAL|PATCH|0.0.05P1.0|0.0.05P2.0|https://example.test/p2.zip|" + digest + "|100\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P2.0|https://example.test/cumulative.zip|" + digest + "|1000\n",
        &errors);
    plan = releasePlanForSku(shortcuts, europeanSku, "Not installed");
    assert(errors.empty() && plan.size() == 2 && plan[1]->downloadUrl.find("cumulative") != std::string::npos);
    plan = releasePlanForSku(shortcuts, europeanSku, "0.0.05P1.0");
    assert(plan.size() == 1 && plan[0]->downloadUrl.find("p2.zip") != std::string::npos);
    errors.clear();
    const auto longJump = parseReleases(
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P1.0|https://example.test/p1.zip|" + digest + "|100\n"
        "UNIVERSAL|PATCH|0.0.05P1.0|0.0.05P2.0|https://example.test/p2.zip|" + digest + "|100\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P50.0|https://example.test/base-to-p50.zip|" + digest + "|5000\n",
        &errors);
    assert(errors.empty());
    assert(releaseAppliesToInstalledVersion(longJump[3], "0.0.05P1.0"));
    assert(releaseAppliesToInstalledVersion(longJump[3], "0.0.05P49.0"));
    assert(!releaseAppliesToInstalledVersion(longJump[3], "0.0.04P99.0"));
    assert(!releaseAppliesToInstalledVersion(longJump[3], "0.0.05P50.0"));
    assert(!releaseAppliesToInstalledVersion(longJump[3], "0.0.06"));
    plan = releasePlanForSku(longJump, europeanSku, "0.0.05P1.0");
    assert(plan.size() == 1 && plan[0]->version == "0.0.05P50.0");
    plan = releasePlanForSku(longJump, europeanSku, "Not installed");
    assert(plan.size() == 2 && plan[0]->kind == ReleaseKind::Full &&
           plan[1]->version == "0.0.05P50.0");
    errors.clear();
    const auto rollup = parseReleases(
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/old-full.zip|" + digest + "|200000000\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P50.0|https://example.test/base-to-p50.zip|" + digest + "|5000\n"
        "UNIVERSAL|FULL|-|0.0.06|https://example.test/rollup-full.zip|" + digest + "|210000000\n",
        &errors);
    assert(errors.empty());
    assert(releaseAppliesToInstalledVersion(rollup[2], "0.0.05P1.0"));
    assert(!releaseAppliesToInstalledVersion(rollup[2], "0.0.06"));
    plan = releasePlanForSku(rollup, europeanSku, "0.0.05P1.0");
    assert(plan.size() == 1 && plan[0]->kind == ReleaseKind::Full &&
           plan[0]->version == "0.0.06");
    errors.clear();
    auto invalidChain = parseReleases(
        "UNIVERSAL|PATCH|0.0.05P1.0|0.0.05|https://example.test/backwards.zip|" + digest + "|1\n",
        &errors);
    assert(invalidChain.empty() && errors.size() == 1);
    errors.clear();
    const auto appReleases = parseAppReleases(
        "# PACKAGE_KEY|KIND|FROM_VERSION|TO_VERSION|DOWNLOAD_URL|SHA256|COMPRESSED_BYTES\n"
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n"
        "#HQ|3|0.0.03-beta|https://example.test/BO2-Unbound-HQ.wuhb|" + digest + "|9000000\r\n"
        "#HQ|4|0.0.04-beta|https://example.test/Unbound-HQ.wuhb|" + digest + "|10000000\n",
        &errors);
    assert(errors.empty() && appReleases.size() == 2);
    assert(appReleases[0].revision == 3 && appReleases[0].version == "0.0.03-beta");
    assert(latestAppRelease(appReleases) == &appReleases[1]);
    assert(latestAppRelease(appReleases)->fileBytes == 10000000);
    assert(latestAppRelease(appReleases)->downloadUrl == "https://example.test/Unbound-HQ.wuhb");
    const auto version15=parseAppReleases("#HQ|66|1.5|https://example.test/Unbound-HQ.wuhb|"+digest+"|43522520\n");
    assert(version15.size()==1&&version15[0].revision==66&&version15[0].version=="1.5");
    assert(sameAppRelease(appReleases[1], appReleases[1]));
    auto conflictingAppRelease = appReleases[1];
    conflictingAppRelease.downloadUrl = "https://example.test/reused-revision.wuhb";
    assert(!sameAppRelease(appReleases[1], conflictingAppRelease));
    errors.clear();
    auto invalidAppReleases = parseAppReleases(
        "#HQ|too-short\n"
        "#HQ|0|0.0.04-beta|https://example.test/hq.wuhb|" + digest + "|100\n"
        "#HQ|4|bad version|https://example.test/hq.wuhb|" + digest + "|100\n"
        "#HQ|5|0.0.05-beta|http://example.test/hq.wuhb|" + digest + "|100\n"
        "#HQ|6|0.0.06-beta|https://example.test/hq.wuhb|bad|100\n"
        "#HQ|7|0.0.07-beta|https://example.test/hq.wuhb|" + digest + "|0\n",
        &errors);
    assert(invalidAppReleases.empty() && errors.size() == 6);
    errors.clear();
    invalidAppReleases = parseAppReleases(
        "#HQ|4|0.0.04-beta|https://example.test/a.wuhb|" + digest + "|100\n"
        "#HQ|4|0.0.04-hotfix|https://example.test/b.wuhb|" + digest + "|101\n",
        &errors);
    assert(invalidAppReleases.size() == 1 && errors.size() == 1);
    assert(errors[0].rfind("Duplicate HQ revision", 0) == 0);
    errors.clear();
    const auto mixedAppReleases = parseAppReleases(
        "not-an-hq-record\n"
        "#HQ release policy comments are ordinary comments\n"
        "UNIVERSAL|BROKEN|content records are ignored by the HQ parser\n"
        "#HQ|5|0.0.05-beta|https://example.test/hq-5.wuhb|" + digest + "|11000000\n",
        &errors);
    assert(mixedAppReleases.size() == 1 && errors.empty());
    assert(latestAppRelease(mixedAppReleases)->revision == 5);
    errors.clear();
    const auto legacyContentView = parseReleases(
        "#HQ|5|0.0.05-beta|https://example.test/hq-5.wuhb|" + digest + "|11000000\n"
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n",
        &errors);
    assert(errors.empty() && legacyContentView.size() == 1);
    assert(legacyContentView[0].version == "0.0.05");
    std::string retainedFeed;
    assert(replaceAppReleaseDirectives(
        "#HQ release policy comment\n"
        "#HQ|3|0.0.03-beta|https://example.test/old.wuhb|" + digest + "|9000000\n"
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + digest + "|200000000\n",
        latestAppRelease(mixedAppReleases), 4096, retainedFeed));
    assert(retainedFeed.find("#HQ release policy comment") != std::string::npos);
    assert(retainedFeed.find("old.wuhb") == std::string::npos);
    errors.clear();
    const auto retainedAppReleases = parseAppReleases(retainedFeed, &errors);
    assert(errors.empty() && retainedAppReleases.size() == 1 &&
           retainedAppReleases[0].revision == 5);
    std::string boundedFeed = "not cleared";
    assert(!replaceAppReleaseDirectives(retainedFeed,latestAppRelease(mixedAppReleases),
                                        16,boundedFeed));
    assert(boundedFeed.empty());
    assert(replaceAppReleaseDirectives("",latestAppRelease(mixedAppReleases),
                                       4096,boundedFeed));
    assert(parseAppReleases(boundedFeed).size()==1);
    assert(latestAppRelease({}) == nullptr);
    errors.clear();
    auto mods = parseWorkshop("dogs|Hell Hounds|Tony|Adds \\| dogs|https://example.test/d.zip|def\n", &errors);
    assert(errors.empty() && mods.size() == 1 && mods[0].description == "Adds | dogs");
    mods = parseWorkshop("dogs|Hell Hounds|Tony|Fun|https://example.test/d.zip|def|42|3|9|Great co-op map!\n", &errors);
    assert(errors.empty() && mods.size() == 1 && mods[0].upvotes == 42 &&
           mods[0].downvotes == 3 && mods[0].commentCount == 9);
    int vote = 0;
    vote = applyWorkshopVote(mods[0], vote, 1);
    assert(vote == 1 && mods[0].upvotes == 43 && mods[0].downvotes == 3);
    vote = applyWorkshopVote(mods[0], vote, 1);
    assert(vote == 1 && mods[0].upvotes == 43 && mods[0].downvotes == 3);
    vote = applyWorkshopVote(mods[0], vote, -1);
    assert(vote == -1 && mods[0].upvotes == 42 && mods[0].downvotes == 4);

    errors.clear();
    const std::string pluginCatalogText =
        "# UNBOUND_PLUGIN_CATALOG_V1\n"
        "PLUGIN|PROTARIUM|Protarium|Protarium Network|BO2 network support.|"
        "https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt|"
        "https://github.com/Protarium-Network/Inkay-GitHub-Release|"
        "wiiu/environments/aroma/modules/Inkay-pretendo.wms,wiiu/environments/aroma/plugins/Inkay-pretendo.wps\n";
    const auto plugins = parsePluginCatalog(pluginCatalogText, &errors);
    assert(errors.empty() && plugins.size() == 1 &&
           plugins[0].managedPaths.size() == 2 && plugins[0].key == "PROTARIUM");
    PluginRelease pluginRelease;
    const std::string pluginFeed =
        "# UNBOUND_PLUGIN_RELEASE_V1\n"
        "PROTARIUM|1|v3.1.0-protarium-sonic-r10-bo2|https://example.test/wiiu.zip|" + digest + "|175651\n"
        "FILE|wiiu/environments/aroma/modules/Inkay-pretendo.wms|" + digest + "|124884\n"
        "FILE|wiiu/environments/aroma/plugins/Inkay-pretendo.wps|" + digest + "|51471\n";
    assert(parsePluginRelease(pluginFeed, plugins[0], pluginRelease, &errors));
    assert(errors.empty() && pluginRelease.revision == 1 && pluginRelease.files.size() == 2 &&
           pluginRelease.compressedBytes == 175651);
    errors.clear();
    const auto modLinkCatalog = parsePluginCatalog(
        "PLUGIN|UNBOUND_MOD_LINK|Unbound Mod Link|Unbound Team|SD mod linking.|"
        "https://example.test/releases-chain-v2.txt|https://example.test|"
        "wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps\n", &errors);
    assert(errors.empty() && modLinkCatalog.size() == 1);
    const std::string sharedReleaseFeed =
        "# UNBOUND_RELEASE_CHAIN_V2\n"
        "#HQ|10|0.0.10-beta|https://example.test/HQ.wuhb|" + digest + "|1000\n"
        "#PLUGIN|UNBOUND_MOD_LINK|1|0.2.0-beta|https://example.test/mod-link.zip|" + digest + "|103542\n"
        "#PLUGIN_FILE|UNBOUND_MOD_LINK|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps|" + digest + "|103228\n"
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/core.zip|" + digest + "|204936880\n";
    PluginRelease sharedPluginRelease;
    assert(parsePluginRelease(sharedReleaseFeed, modLinkCatalog[0], sharedPluginRelease, &errors));
    assert(errors.empty() && sharedPluginRelease.revision == 1 &&
           sharedPluginRelease.version == "0.2.0-beta" &&
           sharedPluginRelease.files.size() == 1 &&
           sharedPluginRelease.files[0].bytes == 103228);
    errors.clear();
    const auto combined=sharedReleaseFeed+"PACK|pack-1|10|Maps|Description\nFILE|pack-1|FULL|-|1.0.0|https://example.test/maps.zip|"+digest+"|100\n";
    assert(parsePluginRelease(combined,modLinkCatalog[0],sharedPluginRelease,&errors)&&errors.empty());
    assert(parseReleases(combined,&errors).size()==1&&errors.empty());
    assert(samePluginRelease(pluginRelease, pluginRelease));
    auto changedPluginRelease = pluginRelease;
    changedPluginRelease.sha256 = std::string(64, 'b');
    assert(!samePluginRelease(pluginRelease, changedPluginRelease));
    errors.clear();
    assert(!parsePluginRelease(
        "PROTARIUM|1|v3.1.0|https://example.test/wiiu.zip|" + digest + "|100\n"
        "FILE|wiiu/environments/aroma/plugins/Inkay-pretendo.wps|" + digest + "|50\n"
        "FILE|wiiu/environments/aroma/plugins/evil.wps|" + digest + "|50\n",
        plugins[0], changedPluginRelease, &errors));
    assert(!errors.empty());
    errors.clear();
    const auto micCatalog = parsePluginCatalog(
        "PLUGIN|GAMEPAD_MIC_REDIRECT|GamePad Mic Redirect|Tony (tonytrawl)|GamePad microphone routing.|"
        "https://raw.githubusercontent.com/tonytrawl/gamepad_mic_redirect/main/release.txt|"
        "https://github.com/tonytrawl/gamepad_mic_redirect|"
        "wiiu/environments/aroma/plugins/GamePadMicRedirect.wps\n", &errors);
    assert(errors.empty() && micCatalog.size() == 1 &&
           micCatalog[0].key == "GAMEPAD_MIC_REDIRECT" &&
           micCatalog[0].managedPaths.size() == 1);
    const std::string micFeed =
        "# UNBOUND_PLUGIN_RELEASE_V1\n"
        "GAMEPAD_MIC_REDIRECT|1|0.4.0|https://github.com/tonytrawl/gamepad_mic_redirect/releases/download/0.4.0/wiiu.zip|" + digest + "|103098\n"
        "FILE|wiiu/environments/aroma/plugins/GamePadMicRedirect.wps|" + digest + "|103822\n";
    PluginRelease micRelease;
    assert(parsePluginRelease(micFeed,micCatalog[0],micRelease,&errors));
    assert(errors.empty() && micRelease.revision == 1 && micRelease.version == "0.4.0" &&
           micRelease.files.size() == 1 && micRelease.files[0].bytes == 103822);
    errors.clear();
    assert(parsePluginCatalog(
        "PLUGIN|BAD|Bad|Bad|Bad|https://example.test/feed|https://example.test|"
        "wiiu/environments/aroma/../../apps/evil.wps\n", &errors).empty());
    assert(errors.size() == 1);

    auto local = parseModload("name=Town Survival\ndescription=Restores the classic map.\nauthor=Tony\n", "town", false);
    assert(local.name == "Town Survival" && local.author == "Tony" &&
           local.syncToken.empty() && !local.installed);
    local = parseModload("name=Town Survival\nversion=1.2.0\nsync_id=town-r8\n", "town", false);
    assert(local.syncToken == "sync_id=town-r8");
    assert(modSyncTokenFromManifest("VERSION = 5\n") == "version=5");
    assert(modSyncTokenFromManifest("version=5\nsync_id=stable\n") == "sync_id=stable");
    assert(modSyncTokenFromManifest("version=\n").empty());
    assert(modSyncTokenFromManifest("sync_id=bad\x01value\n").empty());
    local = parseModload("Nuketown;A custom survival conversion;Mapper", "nuketown", true);
    assert(local.description == "A custom survival conversion" && local.installed);
    local = parseModload("Fallback Name\nFallback description\nFallback author\n", "fallback", false);
    assert(local.name == "Fallback Name" && local.author == "Fallback author");
    auto newsImages = parseNewsImages("# Update\n![Preview](https://example.test/image.png)\n");
    assert(newsImages.size() == 1 && newsImages[0].caption == "Preview");
    assert(parseNewsImages("![Bad](http://example.test/image.png)").empty());
    assert(plainNewsPreview("# Big news\nThis is **bold** and *italic*.\n![Art](https://example.test/a.png)") ==
           "Big news\nThis is bold and italic.");

    assert(updateSmoothedRate(0.0, 1000, 1000) == 1000.0);
    assert(updateSmoothedRate(1000.0, 2000, 1000) > 1333.0 &&
           updateSmoothedRate(1000.0, 2000, 1000) < 1334.0);
    assert(updateSmoothedRate(750.0, 1000, 0) == 750.0);
    std::uint64_t remainingSeconds = 999;
    assert(estimateRemainingSeconds(250, 1000, 100.0, remainingSeconds));
    assert(remainingSeconds == 8); // Round upward instead of promising an early finish.
    assert(estimateRemainingSeconds(1000, 1000, 100.0, remainingSeconds));
    assert(remainingSeconds == 0);
    assert(!estimateRemainingSeconds(1001, 1000, 100.0, remainingSeconds));
    assert(!estimateRemainingSeconds(1, 1000, 0.0, remainingSeconds));
    assert(formatRemainingTime(0) == "Finishing...");
    assert(formatRemainingTime(42) == "Time remaining: 42s");
    assert(formatRemainingTime(125) == "Time remaining: 2m 05s");
    assert(formatRemainingTime(3601) == "Time remaining: 1h 01m");
    assert(formatRemainingTime(3659) == "Time remaining: 1h 01m");
    assert(formatRemainingTime(7500) == "Time remaining: 2h 05m");
    assert(formatRemainingTime(std::numeric_limits<std::uint64_t>::max()) ==
           "Time remaining: 5124095576030431h 01m");

    Sha256 hash; const char* abc = "abc"; hash.update(abc, 3);
    assert(hash.finish() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 streamed;streamed.update(abc,1);streamed.update(abc+1,1);streamed.update(abc+2,1);
    assert(streamed.finish() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 retried;retried.update("discard",7);retried=Sha256{};retried.update(abc,3);
    assert(retried.finish() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 empty;
    empty.update(nullptr,0);
    assert(empty.finish() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string paddingVector = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256 padding;
    padding.update(paddingVector.data(),paddingVector.size());
    assert(padding.finish() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 fullBlock;
    const std::string block(64,'a');
    fullBlock.update(block.data(),block.size());
    assert(fullBlock.finish() == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");

    // A fixed reference digest covers binary input, direct blocks, and a tail.
    std::string binary(4097,'\0');
    for (std::size_t i=0; i<binary.size(); ++i) binary[i]=static_cast<char>(i%256);
    const std::string binaryDigest = "1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc";
    Sha256 direct;
    direct.update(binary.data(),binary.size());
    assert(direct.finish()==binaryDigest);
    assert(direct.finish()==binaryDigest);
    direct.update(abc,3);
    assert(direct.finish()==binaryDigest);
    for (const std::size_t chunk : {1U,3U,55U,56U,63U,64U,65U,127U,128U,129U,1024U}) {
        Sha256 chunks;
        for (std::size_t offset=0; offset<binary.size();) {
            const auto remaining=binary.size()-offset;
            const auto take=remaining<chunk ? remaining : chunk;
            chunks.update(binary.data()+offset,take);
            chunks.update(nullptr,0);
            offset+=take;
        }
        assert(chunks.finish()==binaryDigest);
    }
    for (std::size_t split=0; split<=129; ++split) {
        Sha256 splitHash;
        splitHash.update(binary.data(),split);
        splitHash.update(binary.data()+split,binary.size()-split);
        assert(splitHash.finish()==binaryDigest);
    }
    std::cout << "All model tests passed\n";
}
