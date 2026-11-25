#pragma once

#include "model.hpp"

#include <algorithm>
#include <cctype>

namespace unbound {

inline bool sameInstallPreferences(const InstallPreferences& left,
                                    const InstallPreferences& right) {
    return left.updateDevice == right.updateDevice &&
           left.updateTitleId == right.updateTitleId &&
           left.languageFolder == right.languageFolder &&
           left.languageInstallPending == right.languageInstallPending;
}

inline std::string foldedInstallLanguage(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline const InstallOption* selectedInstallOption(const GameInstall& automatic,
                                                   const std::vector<InstallOption>& options,
                                                   const InstallPreferences& preferences) {
    for (const auto& option : options) {
        if (!option.install.sku) continue;
        if (preferences.updateDevice == Device::Missing) {
            if (!automatic.update.root.empty() &&
                option.install.update.root == automatic.update.root) return &option;
        } else if (option.install.sku->updateTitleId == preferences.updateTitleId &&
                   option.install.update.device == preferences.updateDevice) return &option;
    }
    return nullptr;
}

// Menu preview is pure: all I/O happens once on entry/rescan, not while cycling
// choices or rendering. Applying and starting an install validate again on disk.
inline GameInstall previewInstallSelection(const GameInstall& automatic,
                                            const std::vector<InstallOption>& options,
                                            const InstallPreferences& preferences) {
    const auto* option = selectedInstallOption(automatic, options, preferences);
    GameInstall selected = option ? option->install : automatic;
    selected.targetOverridden = preferences.updateDevice != Device::Missing;
    selected.languageOverridden = !preferences.languageFolder.empty();
    selected.destinationReady = false;
    if (!validInstallPreferences(preferences)) {
        selected.destinationWarning = "Invalid selection. Reset to Auto or choose an available update.";
        return selected;
    }
    if (!option) {
        if (selected.targetOverridden) {
            selected.sku = findSkuByTitle(preferences.updateTitleId);
            selected.update = {preferences.updateDevice, {}, 0, false};
            if (selected.sku) {
                const std::string key = installTargetKey(preferences.updateTitleId, preferences.updateDevice);
                selected.update.root = std::string(preferences.updateDevice == Device::USB ?
                    "storage_usb01:/usr/title/" : "storage_mlc01:/usr/title/") +
                    key.substr(4, 8) + "/" + key.substr(12, 8);
            }
            selected.base = {};
            selected.modVersion = "Not installed";
        }
        if (selected.languageOverridden) selected.languageFolder = preferences.languageFolder;
        selected.destinationWarning = "Selected update unavailable. Choose a detected location or Reset to Auto.";
        return selected;
    }
    if (!option->install.destinationReady) {
        selected.destinationWarning = option->install.destinationWarning.empty() ?
            "This update's version marker could not be read safely. Rescan after fixing storage access." :
            option->install.destinationWarning;
        return selected;
    }
    if (option->languages.empty()) {
        selected.languageFolder.clear();
        selected.destinationWarning = "No supported language directories were found in this update's content folder.";
        return selected;
    }
    const std::string wanted = preferences.languageFolder.empty() ? selected.languageFolder : preferences.languageFolder;
    auto language = std::find_if(option->languages.begin(), option->languages.end(), [&](const auto& folder) {
        return foldedInstallLanguage(folder) == foldedInstallLanguage(wanted);
    });
    if (language == option->languages.end() && selected.languageOverridden) {
        selected.languageFolder = preferences.languageFolder;
        selected.destinationWarning = "Saved language folder is missing. Choose an existing language or use Auto.";
        return selected;
    }
    if (language == option->languages.end()) language = option->languages.begin();
    selected.languageFolder = *language;
    selected.destinationReady = selected.update.present && supportedLanguageFolder(selected.languageFolder);
    selected.destinationWarning.clear();
    return selected;
}

inline bool installLanguageNeedsReplay(const GameInstall& previous,
                                        const GameInstall& selected,
                                        const std::vector<InstallOption>& options) {
    if (!selected.destinationReady || selected.modVersion.empty() ||
        selected.modVersion == "Not installed") return false;
    std::string priorLanguage;
    if (previous.update.root == selected.update.root && !previous.languageFolder.empty()) {
        priorLanguage = previous.languageFolder;
    } else {
        // Another root's shared marker cannot prove that a manually selected
        // language was installed there. Replay once rather than skipping it.
        if (selected.languageOverridden) return true;
        for (const auto& option : options) {
            if (option.install.update.root == selected.update.root) {
                priorLanguage = option.install.languageFolder;
                break;
            }
        }
    }
    return !priorLanguage.empty() &&
           foldedInstallLanguage(priorLanguage) != foldedInstallLanguage(selected.languageFolder);
}

inline std::vector<const Release*> selectedInstallPlan(const std::vector<Release>& releases,
                                                       const GameInstall& selected,
                                                       bool fullReplay) {
    if (!selected.destinationReady || !selected.update.present) return {};
    const std::string newest = latestReleaseVersion(releases, selected.sku);
    if (fullReplay && !selected.modVersion.empty() && selected.modVersion != "Not installed" &&
        !newest.empty() && compareVersions(selected.modVersion, newest) > 0) return {};
    return releasePlanForSku(releases, selected.sku,
                              fullReplay ? "Not installed" : selected.modVersion);
}

} // namespace unbound
