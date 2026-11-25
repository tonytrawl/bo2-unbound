#include "package_cache_reset.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace unbound;
const std::string Hash(64,'a');

std::vector<std::string> expectedPaths() {
    const auto archive=std::string(config::CacheRoot)+"/core-"+Hash+".zip";
    return {archive+".part.resume.unbound-new",archive+".part.resume.unbound-old",
            archive+".part.resume",archive+".part",archive};
}

struct FakeIo {
    std::map<std::string,CoreRecoveryCacheFile> files;
    std::vector<std::string> inspected,removed;
    std::string inspectError,removeError,changedPath;
    bool silentRemoveFailure=false;
    CoreRecoveryCacheFile inspectCache(const std::string& path) {
        inspected.push_back(path);
        if(path==inspectError)return CoreRecoveryCacheFile::Error;
        if(path==changedPath&&std::count(inspected.begin(),inspected.end(),path)>1)
            return CoreRecoveryCacheFile::Other;
        const auto found=files.find(path);
        return found==files.end()?CoreRecoveryCacheFile::Missing:found->second;
    }
    bool removeCache(const std::string& path,std::string& error) {
        removed.push_back(path);
        if(path==removeError){
            if(!silentRemoveFailure)error="Injected remove failure";
            return false;
        }
        assert(files.count(path)&&files.at(path)==CoreRecoveryCacheFile::Regular);
        files.erase(path);return true;
    }
};
FakeIo populated() {
    FakeIo io;
    for(const auto& path:expectedPaths())io.files[path]=CoreRecoveryCacheFile::Regular;
    // Other downloads and repair/game state are outside this operation even
    // when they are non-regular or have superficially similar filenames.
    const std::vector<std::string> foreign{
        std::string(config::CacheRoot)+"/core-"+std::string(64,'b')+".zip",
        std::string(config::CacheRoot)+"/core-"+Hash+".zip.unbound-old",
        std::string(config::CacheRoot)+"/workshop.zip",
        std::string(config::SdRoot)+"/state/core-install.txt",
        std::string(config::SdRoot)+"/state/core-install.txt.unbound-new",
        std::string(config::SdRoot)+"/state/core-install.txt.unbound-old",
        "storage_usb01:/usr/title/0005000e/1010cf00/content/update.txt",
        "storage_usb01:/usr/title/0005000e/1010cf00/code/game.rpl"};
    for(const auto& path:foreign)io.files[path]=CoreRecoveryCacheFile::Other;
    return io;
}

void invalidHashesPerformNoIo() {
    for(const auto& invalid:std::vector<std::string>{"",std::string(63,'a'),std::string(65,'a'),
            std::string(64,'g'),"../"+std::string(61,'a'),
            "C:/outside/package.zip",std::string(63,'a')+"\n",std::string(63,'a')+"/"}){
        auto io=populated();const auto original=io.files;std::string error;
        assert(!clearCorePackageCacheWithIo(invalid,io,error)&&!error.empty());
        assert(io.files==original&&io.inspected.empty()&&io.removed.empty());
    }
}

void exactScopeAndOrder() {
    auto io=populated();auto foreign=io.files;std::string error="old error";
    const auto paths=expectedPaths();
    for(const auto& path:paths)foreign.erase(path);
    assert(clearCorePackageCacheWithIo(std::string(64,'A'),io,error)&&error.empty());
    assert(io.files==foreign&&io.removed==paths&&io.inspected.size()==10);
    assert(std::equal(paths.begin(),paths.end(),io.inspected.begin()));
    assert(std::equal(paths.begin(),paths.end(),io.inspected.begin()+5));
    assert(io.removed.back()==std::string(config::CacheRoot)+"/core-"+Hash+".zip");
    // Retrying an already-cleared scope is harmless and performs no removals.
    io.inspected.clear();io.removed.clear();
    assert(clearCorePackageCacheWithIo(Hash,io,error)&&error.empty());
    assert(io.files==foreign&&io.removed.empty()&&io.inspected.size()==10);
    // A partial download may exist without a canonical ZIP; every resume
    // sidecar still has to disappear before a new download can start.
    io=populated();io.files.erase(paths.back());
    assert(clearCorePackageCacheWithIo(Hash,io,error));
    assert(io.removed==std::vector<std::string>(paths.begin(),paths.end()-1));
    for(const auto& path:paths)assert(!io.files.count(path));
}

void wholeScopePreflight() {
    const auto paths=expectedPaths();
    for(const auto invalidKind:{CoreRecoveryCacheFile::Other,CoreRecoveryCacheFile::Error}){
        for(const auto& blocked:paths){
            auto io=populated();io.files[blocked]=invalidKind;
            const auto original=io.files;std::string error;
            assert(!clearCorePackageCacheWithIo(Hash,io,error)&&!error.empty());
            assert(io.files==original&&io.removed.empty());
        }
    }
    auto io=populated();io.inspectError=paths.back();std::string error;
    assert(!clearCorePackageCacheWithIo(Hash,io,error));
    assert(io.inspected==paths&&io.removed.empty());
    // Other represents directories, filesystem references/links and device
    // nodes alike. No such object is authorized by a matching cache basename.
    io=populated();io.changedPath=paths.front();const auto unchanged=io.files;
    assert(!clearCorePackageCacheWithIo(Hash,io,error));
    assert(io.files==unchanged&&io.removed.empty());
    io=populated();io.changedPath=paths[3];
    assert(!clearCorePackageCacheWithIo(Hash,io,error));
    assert(io.removed==std::vector<std::string>(paths.begin(),paths.begin()+3));
    assert(io.files.count(paths[3])&&io.files.count(paths[4]));
}

void removalFailuresKeepUnreachedFiles() {
    const auto paths=expectedPaths();
    for(std::size_t failAt=0;failAt<paths.size();++failAt){
        for(const bool silent:{false,true}){
            auto io=populated();io.removeError=paths[failAt];io.silentRemoveFailure=silent;
            auto expected=io.files;std::string error;
            assert(!clearCorePackageCacheWithIo(Hash,io,error)&&!error.empty());
            assert(io.removed.size()==failAt+1);
            for(std::size_t done=0;done<failAt;++done)expected.erase(paths[done]);
            assert(io.files==expected);
            // No removal failure authorizes deleting the journal, game marker
            // or remaining data. A later explicit retry can finish cleanup.
            io.removeError.clear();io.removed.clear();
            assert(clearCorePackageCacheWithIo(Hash,io,error));
            for(const auto& path:paths)assert(!io.files.count(path));
        }
    }
}
}

int main() {
    invalidHashesPerformNoIo();exactScopeAndOrder();wholeScopePreflight();removalFailuresKeepUnreachedFiles();
    std::cout<<"Scoped package-cache hash validation, preflight, resume reset and removal-failure tests passed\n";
}
