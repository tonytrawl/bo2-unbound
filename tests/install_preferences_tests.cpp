#include "install_selection.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace unbound;
    InstallPreferences preferences;
    std::string error;
    assert(parseInstallPreferences("mod_link=on\nplugin_auto_update.mic=on\n", preferences, error));
    assert(validInstallPreferences(preferences) && preferences.updateTitleId == 0);
    assert(installTargetKey(0, Device::Missing) == "auto");
    assert(installTargetKey(SupportedSkus[1].updateTitleId, Device::USB) == "usb:0005000e10113400");
    for (const auto& invalid : {
            "install_target=sd:0005000e10113400\n", "install_target=usb:0005000010113400\n",
            "install_target=usb:0005000c10113400\n", "install_target=mlc:0005000e00000000\n",
            "install_target=usb:0005000e1011340g\n", "install_target=../content\n",
            "install_target=usb:0005000e101134000\n", "install_target=\n",
            "install_target=auto\nINSTALL_TARGET=auto\n", "install_language=../../english\n",
            "install_language=english/path\n", "install_language=english\ninstall_language=french\n",
            "install_language_pending=maybe\n", "install_language_pending=on\ninstall_language_pending=off\n"}) {
        assert(!parseInstallPreferences(invalid, preferences, error));
        assert(!error.empty() && preferences.updateTitleId == 0);
    }
    for (const auto& sku : SupportedSkus) {
        for (const auto device : {Device::MLC, Device::USB}) {
            const std::string text = "install_target=" + installTargetKey(sku.updateTitleId, device) +
                                     "\ninstall_language=Japanese\ninstall_language_pending=on\n";
            assert(parseInstallPreferences(text, preferences, error));
            assert(preferences.updateTitleId == sku.updateTitleId && preferences.updateDevice == device);
            assert(preferences.languageFolder == "Japanese" && preferences.languageInstallPending);
        }
    }
    const std::string old = "# user settings\r\nmod_link=on\r\nplugin_auto_update.mic=off\r\n"
                            "install_target=auto\r\ninstall_language=auto\r\ninstall_language_pending=off\r\n";
    const std::string saved = replaceInstallPreferences(old, preferences);
    assert(saved.find("mod_link=on\n") != std::string::npos);
    assert(saved.find("plugin_auto_update.mic=off\n") != std::string::npos);
    InstallPreferences loaded;
    assert(parseInstallPreferences(saved, loaded, error) && sameInstallPreferences(loaded, preferences));
    assert(parseInstallPreferences(replaceInstallPreferences(saved, {}), loaded, error));
    assert(loaded.updateDevice == Device::Missing && loaded.languageFolder.empty() && !loaded.languageInstallPending);
    assert(supportedLanguageFolder("English") && supportedLanguageFolder("japanese") && supportedLanguageFolder("fulljap"));
    assert(!supportedLanguageFolder("japenese") && !supportedLanguageFolder("english/../code"));
    std::string localized;
    assert(localizeArchivePath("update/content/@language/file.ff", "French", localized));
    assert(localized == "update/content/French/file.ff");
    assert(localizeArchivePath("update/content/@language/file.ff", "fulljap", localized));
    assert(localized == "update/content/fulljap/file.ff");

    GameInstall automatic;
    automatic.sku = &SupportedSkus[1];
    automatic.consoleRegion = Region::USA;
    automatic.base = {Device::MLC, "storage_mlc01:/usr/title/00050000/10113400", 0, true};
    automatic.update = {Device::MLC, "storage_mlc01:/usr/title/0005000e/10113400", 128, true};
    automatic.languageFolder = "english";
    automatic.modVersion = "0.0.05P2.1";
    InstallOption mlc{automatic, {"english", "French"}};
    InstallOption usb = mlc;
    usb.install.update = {Device::USB, "storage_usb01:/usr/title/0005000e/10113400", 128, true};
    usb.install.base = {Device::USB, "storage_usb01:/usr/title/00050000/10113400", 0, true};
    usb.install.modVersion = "0.0.05";
    std::vector<InstallOption> options{mlc, usb};
    preferences = {};
    auto selected = previewInstallSelection(automatic, options, preferences);
    assert(selected.destinationReady && selected.update.device == Device::MLC);
    preferences.updateDevice = Device::USB;
    preferences.updateTitleId = automatic.sku->updateTitleId;
    selected = previewInstallSelection(automatic, options, preferences);
    assert(selected.destinationReady && selected.update.device == Device::USB);
    assert(selected.modVersion == "0.0.05" && selected.sku == automatic.sku);
    assert(!installLanguageNeedsReplay(automatic, selected, options));
    preferences.languageFolder = "french";
    selected = previewInstallSelection(automatic, options, preferences);
    assert(selected.destinationReady && selected.languageFolder == "French");
    assert(installLanguageNeedsReplay(automatic, selected, options));
    preferences.languageInstallPending = true;
    assert(parseInstallPreferences(replaceInstallPreferences(saved, preferences), loaded, error));
    assert(loaded.languageInstallPending); // Survives cancellation and a new session.
    auto missing = previewInstallSelection(automatic, {mlc}, preferences);
    assert(!missing.destinationReady && !missing.update.present);
    assert(missing.update.device == Device::USB && missing.sku == automatic.sku);
    assert(missing.update.root == usb.install.update.root);
    preferences.languageFolder = "japanese";
    missing = previewInstallSelection(automatic, options, preferences);
    assert(!missing.destinationReady && missing.languageFolder == "japanese");
    selected = previewInstallSelection(automatic, options, {});
    assert(selected.destinationReady && selected.update.device == Device::MLC);
    GameInstall previouslyFrench = automatic;
    previouslyFrench.languageFolder = "French";
    assert(installLanguageNeedsReplay(previouslyFrench, selected, options)); // Reset Auto needs replay too.
    InstallOption onlyFrench = mlc;
    onlyFrench.languages = {"French"};
    selected = previewInstallSelection(automatic, {onlyFrench}, {});
    assert(selected.destinationReady && selected.languageFolder == "French");
    InstallOption empty = mlc;
    empty.languages.clear();
    assert(!previewInstallSelection(automatic, {empty}, {}).destinationReady);
    InstallOption unreadableMarker = mlc;
    unreadableMarker.install.destinationReady = false;
    unreadableMarker.install.destinationWarning = "Could not read update.txt";
    assert(!previewInstallSelection(automatic, {unreadableMarker}, {}).destinationReady);
    const auto unreadablePreview = previewInstallSelection(automatic, {unreadableMarker}, {});
    assert(unreadablePreview.destinationWarning == unreadableMarker.install.destinationWarning);

    // A preview uses the scanner's actual automatic language, not an English
    // default. It must agree with the final locale/SKU selection.
    InstallOption autoFrench = mlc;
    autoFrench.install.languageFolder = "French";
    selected = previewInstallSelection(automatic, {autoFrench}, {});
    assert(selected.destinationReady && selected.languageFolder == "French");

    // Choosing another language and then reverting the draft must not leave a
    // newly invented replay flag. An already saved unfinished replay is separate.
    preferences = {};
    preferences.languageFolder = "French";
    selected = previewInstallSelection(automatic, options, preferences);
    assert(installLanguageNeedsReplay(automatic, selected, options));
    preferences.languageFolder.clear();
    selected = previewInstallSelection(automatic, options, preferences);
    assert(!installLanguageNeedsReplay(automatic, selected, options));

    // The global marker cannot prove which language another storage copy has.
    // Even if its automatic preference matches, an explicit language on a
    // different, already installed root needs the complete package chain.
    InstallOption usbFrench = usb;
    usbFrench.install.languageFolder = "French";
    preferences.updateDevice = Device::USB;
    preferences.updateTitleId = automatic.sku->updateTitleId;
    preferences.languageFolder = "French";
    selected = previewInstallSelection(automatic, {mlc, usbFrench}, preferences);
    assert(selected.destinationReady && selected.languageFolder == "French");
    assert(installLanguageNeedsReplay(automatic, selected, {mlc, usbFrench}));

    // A missing saved target for a different SKU must not inherit a previous
    // game's launchable base installation or silently fall back to internal.
    preferences.updateTitleId = SupportedSkus[4].updateTitleId;
    preferences.languageFolder = "fulljap";
    missing = previewInstallSelection(automatic, options, preferences);
    assert(!missing.destinationReady && !missing.update.present && !missing.base.present);
    assert(missing.sku == &SupportedSkus[4] && missing.update.device == Device::USB);

    GameInstall japan;
    japan.sku = &SupportedSkus[4];
    japan.update = {Device::USB, "storage_usb01:/usr/title/0005000e/1011b400", 96, true};
    japan.languageFolder = "fulljap";
    japan.modVersion = "0.0.05P2.1";
    const InstallOption japanOption{japan, {"fulljap"}};
    selected = previewInstallSelection(japan, {japanOption}, {});
    assert(selected.destinationReady && selected.languageFolder == "fulljap");
    assert(!installLanguageNeedsReplay(japan, selected, {japanOption}));

    const std::string hash(64, 'a');
    const auto releases = parseReleases(
        "UNIVERSAL|FULL|-|0.0.05|https://example.test/full.zip|" + hash + "|100\n"
        "UNIVERSAL|PATCH|0.0.05|0.0.05P2.1|https://example.test/patch.zip|" + hash + "|10\n");
    selected = previewInstallSelection(automatic, options, {});
    assert(selectedInstallPlan(releases, selected, false).empty());
    const auto replay = selectedInstallPlan(releases, selected, true);
    assert(replay.size() == 2 && replay[0]->kind == ReleaseKind::Full && replay[1]->kind == ReleaseKind::Patch);
    // A stale or rolled-back feed must never authorize a language repair to
    // downgrade code/content already installed by a newer release.
    selected.modVersion = "0.0.05P3.0";
    assert(selectedInstallPlan(releases, selected, false).empty());
    assert(selectedInstallPlan(releases, selected, true).empty());
    selected.modVersion = "0.0.05P2.1";
    assert(selectedInstallPlan(releases, selected, true).size() == 2);
    selected.modVersion = "Not installed";
    assert(selectedInstallPlan(releases, selected, true).size() == 2);
    selected.destinationReady = false;
    assert(selectedInstallPlan(releases, selected, true).empty());
    std::cout << "Install preferences, destination/language selection and replay tests passed\n";
}
