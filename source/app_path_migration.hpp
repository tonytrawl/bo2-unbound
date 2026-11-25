#pragma once

#include "platform.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace unbound {

inline bool sameSdAppPath(std::string_view running, std::string_view expected) {
    constexpr std::string_view mount = "fs:/vol/external01/";
    if (expected.starts_with(mount)) expected.remove_prefix(mount.size());
    if (running.starts_with(mount)) running.remove_prefix(mount.size());
    if (running.starts_with("/vol/external01/")) running.remove_prefix(sizeof("/vol/external01/")-1);
    if (running.starts_with("sd:")) running.remove_prefix(3);
    if (running.starts_with('/')) running.remove_prefix(1);
    return std::equal(running.begin(), running.end(), expected.begin(), expected.end(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        });
}

template<class Io>
AppPathMigration migrateHqPath(std::string_view running, Io& io, std::string& error) {
    const std::string oldRoot = config::LegacyHqSdRoot;
    const std::string newRoot = config::PublishedSdRoot;
    const std::string oldName = "/BO2-Unbound-HQ.wuhb";
    const std::string intermediate = newRoot + oldName;
    const std::string canonical = config::AppWuhbPath;
    const std::string legacy = oldRoot + oldName;
    const std::string renamedLegacy = oldRoot + "/" + config::AppWuhbName;
    const bool fromOldFolder = sameSdAppPath(running, legacy) ||
                               sameSdAppPath(running, renamedLegacy);
    const bool alreadyRenamed = sameSdAppPath(running, renamedLegacy);
    if (!fromOldFolder && !sameSdAppPath(running, intermediate))
        return AppPathMigration::Unchanged;
    const auto& source = fromOldFolder ? (alreadyRenamed ? renamedLegacy : legacy) : intermediate;
    if (!io.validWuhb(source)) {
        error = "The running HQ bundle could not be verified on SD.";
        return AppPathMigration::Failed;
    }
    if (!io.missing(canonical)) {
        error = "sd:/wiiu/apps/Unbound-HQ/Unbound-HQ.wuhb already exists or is unreadable. "
                "Both copies were preserved. Open Unbound HQ from the new folder.";
        return AppPathMigration::Failed;
    }

    const bool moveFolder = fromOldFolder && io.missing(newRoot);
    if (fromOldFolder && !moveFolder && !io.mergeState(oldRoot, newRoot, error))
        return AppPathMigration::Failed;
    if (!io.release(error)) return AppPathMigration::Failed;
    if (!moveFolder)
        return io.rename(source, canonical, error) ? AppPathMigration::Migrated : AppPathMigration::Failed;
    if (!io.rename(oldRoot, newRoot, error)) return AppPathMigration::Failed;
    if (alreadyRenamed || io.rename(intermediate, canonical, error))
        return AppPathMigration::Migrated;

    // A restart between folder and file moves leaves a recognized intermediate path.
    std::string rollbackError;
    if (!io.rename(newRoot, oldRoot, rollbackError))
        error += "\n\nThe old filename is still available inside sd:/wiiu/apps/Unbound-HQ. Open it to retry.";
    return AppPathMigration::Failed;
}

} // namespace unbound
