#include "plugin_policy.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace unbound;

    WorkshopPlugin protarium{"PROTARIUM", "Protarium", "Protarium Network",
        "Paired Inkay Aroma module and plugin.",
        "https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt",
        "https://github.com/Protarium-Network/Inkay-GitHub-Release",
        {"wiiu/environments/aroma/modules/Inkay-pretendo.wms",
         "wiiu/environments/aroma/plugins/Inkay-pretendo.wps"}};
    assert(supportsStartupPluginAutoUpdate(protarium));
    assert(!shouldCheckPluginUpdateAtStartup(protarium, false));
    assert(shouldCheckPluginUpdateAtStartup(protarium, true));

    for (const char* key : {"UNBOUND_MOD_LINK", "GAMEPAD_MIC_REDIRECT"}) {
        auto plugin = protarium;
        plugin.key = key;
        assert(supportsStartupPluginAutoUpdate(plugin));
        assert(!shouldCheckPluginUpdateAtStartup(plugin, false));
        assert(shouldCheckPluginUpdateAtStartup(plugin, true));
    }
    for (const char* key : {"OTHER_PLUGIN", "protarium", "PROTARIUM_CLONE", ""}) {
        auto plugin = protarium;
        plugin.key = key;
        assert(!supportsStartupPluginAutoUpdate(plugin));
        assert(!shouldCheckPluginUpdateAtStartup(plugin, false));
        assert(!shouldCheckPluginUpdateAtStartup(plugin, true));
    }

    // Snapshot of the publisher's immutable release verified for r28. Uses the
    // real production parser; no network request is needed to run the test.
    const std::string feed =
        "# UNBOUND_PLUGIN_RELEASE_V1\n"
        "PROTARIUM|12|v3.1.0-protarium-jd|https://github.com/Protarium-Network/Inkay-GitHub-Release/releases/download/v3.1.0-protarium-jd/wiiu.zip|2ed3c47381e50f8284ea96b51440af16d773b6da4624d21718ec1f2be6f087fc|181519\n"
        "FILE|wiiu/environments/aroma/modules/Inkay-pretendo.wms|f742dcf288b604f818ddb0d63b46a6f44b5a3a4292d96fc05e3d5c2780d121e8|130756\n"
        "FILE|wiiu/environments/aroma/plugins/Inkay-pretendo.wps|4559dcd6dad1d083764ff47d7052e1012de092d77ab2f8aaa3e77eec4f8e02d2|51493\n";
    PluginRelease release;
    std::vector<std::string> errors;
    assert(parsePluginRelease(feed, protarium, release, &errors));
    assert(errors.empty());
    assert(release.pluginKey == "PROTARIUM" && release.revision == 12);
    assert(release.version == "v3.1.0-protarium-jd");
    assert(release.compressedBytes == 181519 && release.files.size() == 2);
    assert(release.files[0].archivePath == protarium.managedPaths[0]);
    assert(release.files[0].bytes == 130756);
    assert(release.files[1].archivePath == protarium.managedPaths[1]);
    assert(release.files[1].bytes == 51493);

    auto missingPair = protarium;
    missingPair.managedPaths.pop_back();
    errors.clear();
    assert(!parsePluginRelease(feed, missingPair, release, &errors));
    assert(!errors.empty() && release.files.empty());

    auto unexpectedPath = protarium;
    unexpectedPath.managedPaths[1] = "wiiu/environments/aroma/plugins/Inkay.wps";
    errors.clear();
    assert(!parsePluginRelease(feed, unexpectedPath, release, &errors));
    assert(!errors.empty() && release.files.empty());

    std::cout << "Plugin startup ON/OFF policy and exact Protarium publisher feed tests passed\n";
}
