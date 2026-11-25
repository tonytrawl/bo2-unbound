#pragma once

#include "core_recovery_restart.hpp"

#include <array>
#include <string>

namespace unbound {

// Clear one explicitly rejected core download, never its repair journal or a
// game/version file. The caller decides whether verification definitively
// rejected the payload; cancellation/read errors must not invoke this reset.
// Io must inspect without following links and refuse non-regular deletions.
template<class Io>
bool clearCorePackageCacheWithIo(const std::string& sha,Io& io,std::string& error) {
    error.clear();
    if(!core_recovery_detail::digest(sha)){
        error="The package cache hash is invalid. No download files were removed.";
        return false;
    }
    const std::string archive=std::string(config::gameCacheRoot())+"/core-"+
        core_recovery_detail::folded(sha)+".zip";
    // Remove resume metadata before its data, and the canonical ZIP last. A
    // partial cleanup can never leave metadata pointing at deleted partial data.
    const std::array<std::string,5> paths{{archive+".part.resume.unbound-new",
        archive+".part.resume.unbound-old",archive+".part.resume",archive+".part",archive}};
    for(const auto& path:paths){
        const auto kind=io.inspectCache(path);
        if(kind!=CoreRecoveryCacheFile::Missing&&kind!=CoreRecoveryCacheFile::Regular){
            error=(kind==CoreRecoveryCacheFile::Error?
                "Could not inspect the selected package cache: ":
                "The selected package cache contains a non-regular path: ")+path+
                "\nNo download files were removed.";
            return false;
        }
    }
    for(const auto& path:paths){
        const auto kind=io.inspectCache(path);
        if(kind==CoreRecoveryCacheFile::Missing)continue;
        if(kind!=CoreRecoveryCacheFile::Regular){
            error="The selected package cache changed during cleanup: "+path+
                "\nIts repair record was left unchanged.";
            return false;
        }
        if(!io.removeCache(path,error)){
            if(error.empty())error="Could not remove the selected package cache file: "+path;
            return false;
        }
    }
    error.clear();
    return true;
}

} // namespace unbound
