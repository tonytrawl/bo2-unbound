#pragma once

#include "model.hpp"

namespace unbound {

// Share the supported set between startup, the action handler and its UI.
// Unknown catalog entries remain manual-only, even with a saved ON setting.
inline bool supportsStartupPluginAutoUpdate(const WorkshopPlugin& plugin) {
    return plugin.key == "UNBOUND_MOD_LINK" ||
           plugin.key == "GAMEPAD_MIC_REDIRECT" ||
           plugin.key == "PROTARIUM";
}

// Read the local setting before this gate. OFF must never fetch a publisher's
// feed at startup. The required Mod Link caller explicitly supplies true.
inline bool shouldCheckPluginUpdateAtStartup(const WorkshopPlugin& plugin,
                                            bool autoUpdateEnabled) {
    return supportsStartupPluginAutoUpdate(plugin) && autoUpdateEnabled;
}

} // namespace unbound
