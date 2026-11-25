#pragma once

#include "core_recovery.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace unbound {

enum class CoreRecoveryCacheFile { Missing, Regular, Other, Error };

// Io must inspect without following links, remove only bounded regular cache
// files (missing is success), and save the small journal atomically. It never
// receives a game-file path or an instruction to remove a journal/marker.
//
// Purge precedes the journal commit deliberately: after any failure or power
// cut, the old saved plan is still authoritative and can redownload its ZIPs.
// Once the replacement is committed its entire cache has already been cleared,
// so it needs no separate durable "fresh download" flag.
template<class Io>
bool restartCoreRecoveryWithIo(const CoreRecoveryRecord& expected,
                                const CoreRecoveryRecord& replacement,
                                Io& io,std::string& error) {
    using core_recovery_detail::fail;
    error.clear();
    if(!validateCoreRecovery(expected,error)||!validateCoreRecovery(replacement,error))return false;
    if(expected.device!=replacement.device||expected.titleId!=replacement.titleId||
       expected.language!=replacement.language)
        return fail(error,"Download fresh must keep the unfinished installation's exact storage, title and language.");
    if(!replacement.replayFull||replacement.currentStep!=0||
       replacement.phase!=CoreRecoveryPhase::Installing||replacement.plan.front().kind!=ReleaseKind::Full)
        return fail(error,"Download fresh requires a complete full-package replay at its first install step.");
    if(compareVersions(replacement.plan.back().version,expected.plan.back().version)<0)
        return fail(error,"The replacement package chain is older than the unfinished installation. Its repair record was retained.");

    std::string expectedText,replacementText;
    if(!serializeCoreRecovery(expected,expectedText,error)||
       !serializeCoreRecovery(replacement,replacementText,error))return false;
    const auto savedStillMatches=[&]() {
        CoreRecoveryRecord current;
        if(!io.read(current,error)){
            if(error.empty())error="Could not read the saved installation. Its repair record was retained.";
            return false;
        }
        std::string savedText;
        if(!serializeCoreRecovery(current,savedText,error))return false;
        if(savedText!=expectedText)
            return fail(error,"The install repair record changed. Download fresh did not replace it.");
        return true;
    };
    if(!savedStillMatches())return false;

    std::vector<std::string> paths;
    paths.reserve(replacement.plan.size()*5);
    for(const auto& release:replacement.plan){
        // The validated 64-digit SHA, never a URL/version or caller-supplied
        // filesystem path, chooses the one approved archive basename.
        const std::string archive=coreRecoveryArchivePath(release);
        const std::string exact=std::string(config::gameCacheRoot())+"/core-"+
            core_recovery_detail::folded(release.sha256)+".zip";
        if(archive.empty()||archive!=exact)
            return fail(error,"The replacement has an invalid cache path. No download cache was cleared.");
        for(const char* suffix:{"",".part",".part.resume",".part.resume.unbound-new",".part.resume.unbound-old"}){
            const std::string path=archive+suffix;
            if(std::find(paths.begin(),paths.end(),path)==paths.end())paths.push_back(path);
        }
    }
    // Check the entire scope before removing even its first file. Directories,
    // links, unreadable objects and unexpected device entries are never erased.
    for(const auto& path:paths){
        const auto kind=io.inspectCache(path);
        if(kind!=CoreRecoveryCacheFile::Regular&&kind!=CoreRecoveryCacheFile::Missing)
            return fail(error,kind==CoreRecoveryCacheFile::Error?
                "Could not inspect a fresh-download cache file. The repair record was retained.":
                "A fresh-download cache path is not a regular file. The repair record was retained.");
    }
    for(const auto& path:paths){
        // Recheck at the deletion boundary as well as in the whole-plan
        // preflight; the real adapter must also refuse to follow links.
        const auto kind=io.inspectCache(path);
        if(kind==CoreRecoveryCacheFile::Missing)continue;
        if(kind!=CoreRecoveryCacheFile::Regular)
            return fail(error,"A download cache path changed during the reset. The repair record was retained.");
        if(!io.removeCache(path,error)){
            if(error.empty())error="Could not clear the selected download cache. The repair record was retained.";
            return false;
        }
    }
    // An unrelated journal change during cache cleanup must not be overwritten.
    if(!savedStillMatches())return false;
    if(!io.save(replacement,error)){
        if(error.empty())error="Could not save the fresh install plan. The previous repair record was retained.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace unbound
