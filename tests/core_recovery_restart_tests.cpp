#include "core_recovery_restart.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace unbound;

Release full(const std::string& version="0.1.0",char hash='a') {
    return {"UNIVERSAL",ReleaseKind::Full,"",version,
            "https://github.com/publisher/mod/releases/download/full/payload.zip",std::string(64,hash),200000};
}
Release patch(const std::string& from="0.1.0",const std::string& version="0.1.0P1.0",char hash='b') {
    return {"UNIVERSAL",ReleaseKind::Patch,from,version,
            "https://github.com/publisher/mod/releases/download/patch/payload.zip",std::string(64,hash),8000};
}
CoreRecoveryRecord record() {
    CoreRecoveryRecord result;
    result.device=Device::USB;result.titleId=SupportedSkus[0].updateTitleId;
    result.language="english";result.beforeVersion="Not installed";
    result.phase=CoreRecoveryPhase::Installing;result.plan={full(),patch()};
    return result;
}
CoreRecoveryRecord restart() {
    auto result=record();result.replayFull=true;
    result.plan={full("0.2.0",'c'),patch("0.2.0","0.2.0P1.0",'d')};
    return result;
}
std::string serialized(const CoreRecoveryRecord& record) {
    std::string text,error;assert(serializeCoreRecovery(record,text,error));return text;
}
std::vector<std::string> cachePaths(const CoreRecoveryRecord& record) {
    std::vector<std::string> paths;
    for(const auto& release:record.plan){
        const auto archive=coreRecoveryArchivePath(release);
        for(const char* suffix:{"",".part",".part.resume",".part.resume.unbound-new",".part.resume.unbound-old"}){
            const auto path=archive+suffix;
            if(std::find(paths.begin(),paths.end(),path)==paths.end())paths.push_back(path);
        }
    }
    return paths;
}

struct FakeIo {
    CoreRecoveryRecord saved=record();
    std::map<std::string,CoreRecoveryCacheFile> files;
    std::vector<std::string> inspected,removed,events;
    std::string failInspect,failRemove,changeOnRecheck;
    bool failRead=false,failSave=false,changeJournalAfterRemoval=false;
    unsigned reads=0,saves=0,failReadAt=0;
    bool read(CoreRecoveryRecord& result,std::string& error) {
        events.push_back("read");++reads;
        if(failRead||reads==failReadAt){error="Injected journal read failure";return false;}
        result=saved;return true;
    }
    CoreRecoveryCacheFile inspectCache(const std::string& path) {
        events.push_back("inspect:"+path);inspected.push_back(path);
        if(path==failInspect)return CoreRecoveryCacheFile::Error;
        if(path==changeOnRecheck&&std::count(inspected.begin(),inspected.end(),path)>1)
            return CoreRecoveryCacheFile::Other;
        const auto found=files.find(path);
        return found==files.end()?CoreRecoveryCacheFile::Missing:found->second;
    }
    bool removeCache(const std::string& path,std::string& error) {
        events.push_back("remove:"+path);removed.push_back(path);
        if(path==failRemove){error="Injected cache remove failure";return false;}
        files.erase(path);
        if(changeJournalAfterRemoval)saved.plan.front().compressedBytes++;
        return true;
    }
    bool save(const CoreRecoveryRecord& replacement,std::string& error) {
        events.push_back("save");++saves;
        if(failSave){error="Injected atomic save failure";return false;}
        saved=replacement;return true;
    }
};
FakeIo populated() {
    FakeIo io;
    for(const auto& path:cachePaths(restart()))io.files[path]=CoreRecoveryCacheFile::Regular;
    for(const auto& path:cachePaths(record()))io.files[path]=CoreRecoveryCacheFile::Regular;
    for(const std::string& path:{std::string(config::CacheRoot)+"/news.png",
                               std::string(config::CacheRoot)+"/other.zip",
                               std::string(config::CacheRoot)+"/core-"+std::string(64,'e')+".zip",
                               std::string(config::SdRoot)+"/state/core-install.txt",
                               std::string(config::SdRoot)+"/state/core-install.txt.unbound-new",
                               std::string(config::SdRoot)+"/state/core-install.txt.unbound-old",
                               coreRecoveryTargetRoot(record())+"/content/update.txt",
                               coreRecoveryTargetRoot(record())+"/code/test.rpl"})
        io.files[path]=CoreRecoveryCacheFile::Regular;
    return io;
}

void validationTests() {
    std::string error;
    const auto original=record();
    for(unsigned scenario=0;scenario<16;++scenario){
        auto expected=original,replacement=restart();
        switch(scenario){
            case 0:replacement.device=Device::MLC;break;
            case 1:replacement.titleId=SupportedSkus[1].updateTitleId;break;
            case 2:replacement.language="french";break;
            case 3:replacement.language="English";break; // Exact chosen folder spelling matters.
            case 4:replacement.replayFull=false;break;
            case 5:replacement.phase=CoreRecoveryPhase::BeforeDownload;break;
            case 6:replacement.currentStep=1;replacement.beforeVersion=replacement.plan[0].version;break;
            case 7:replacement.plan.clear();break;
            case 8:replacement.plan.erase(replacement.plan.begin());replacement.beforeVersion="0.2.0";break;
            case 9:replacement.plan={full("0.1.0",'c')};break; // Missing final patch would downgrade.
            case 10:replacement.plan[1].fromVersion="0.3.0";break;
            case 11:replacement.plan[0].sha256="../"+std::string(61,'a');break;
            case 12:replacement.plan[0].sha256="C:/outside/cache.zip";break;
            case 13:replacement.plan[0].downloadUrl="file:///unrelated/path";break;
            case 14:expected.currentStep=expected.plan.size();
                    expected.beforeVersion=expected.plan.back().version;break;
            case 15:expected.language="../english";break;
        }
        auto io=populated();const auto files=io.files;const auto saved=serialized(io.saved);
        assert(!restartCoreRecoveryWithIo(expected,replacement,io,error)&&!error.empty());
        assert(io.events.empty()&&io.files==files&&serialized(io.saved)==saved);
    }
}

void exactJournalTests() {
    std::string error;
    auto io=populated();io.failRead=true;
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.inspected.empty()&&io.removed.empty()&&io.saves==0);
    for(unsigned field=0;field<10;++field){
        io=populated();
        switch(field){
            case 0:io.saved.device=Device::MLC;break;
            case 1:io.saved.titleId=SupportedSkus[1].updateTitleId;break;
            case 2:io.saved.language="french";break;
            case 3:io.saved.phase=CoreRecoveryPhase::BeforeDownload;break;
            case 4:io.saved.beforeVersion="0.0.90";break;
            case 5:io.saved.replayFull=true;break;
            case 6:io.saved.plan[0].compressedBytes++;break;
            case 7:io.saved.plan[0].downloadUrl+="?new=1";break;
            case 8:io.saved.plan[0].sha256=std::string(64,'e');break;
            case 9:io.saved.currentStep=1;io.saved.beforeVersion=io.saved.plan[0].version;break;
        }
        const auto files=io.files;const auto changed=serialized(io.saved);
        assert(!restartCoreRecoveryWithIo(record(),restart(),io,error)&&!error.empty());
        assert(io.inspected.empty()&&io.removed.empty()&&io.saves==0&&io.files==files);
        assert(serialized(io.saved)==changed);
    }
    io=populated();io.saved.language="../invalid";
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.inspected.empty()&&io.removed.empty()&&io.saves==0);
}

void preflightTests() {
    std::string error;
    const auto paths=cachePaths(restart());
    for(const auto kind:{CoreRecoveryCacheFile::Other,CoreRecoveryCacheFile::Error}){
        auto io=populated();io.files[paths.back()]=kind;
        const auto files=io.files;const auto saved=serialized(io.saved);
        assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
        assert(io.inspected==paths&&io.removed.empty()&&io.saves==0&&io.files==files);
        assert(serialized(io.saved)==saved);
    }
    auto io=populated();io.failInspect=paths.back();
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.removed.empty()&&io.saves==0);
    io=populated();io.changeOnRecheck=paths.front();
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.removed.empty()&&io.saves==0);
}

void purgeAndCommitTests() {
    std::string error="stale error";
    const auto paths=cachePaths(restart());
    auto io=populated();auto remaining=io.files;
    for(const auto& path:paths)remaining.erase(path);
    assert(restartCoreRecoveryWithIo(record(),restart(),io,error)&&error.empty());
    assert(io.files==remaining&&io.removed==paths&&io.saves==1&&io.reads==2);
    assert(serialized(io.saved)==serialized(restart()));
    assert(io.saved.currentStep==0&&io.saved.phase==CoreRecoveryPhase::Installing);
    assert(io.events.front()=="read"&&io.events.back()=="save");
    const auto firstRemove=std::find_if(io.events.begin(),io.events.end(),[](const auto& event){return event.rfind("remove:",0)==0;});
    assert(static_cast<std::size_t>(std::count_if(io.events.begin(),firstRemove,[](const auto& event){return event.rfind("inspect:",0)==0;}))==paths.size()+1);
    // Both the old-plan ZIPs and every foreign file, including the version
    // marker, journal sidecars and game payloads, remain completely untouched.
    for(const auto& path:cachePaths(record()))assert(io.files.count(path)==1);
    for(std::size_t failAt=0;failAt<paths.size();++failAt){
        io=populated();io.failRemove=paths[failAt];
        const auto saved=serialized(io.saved);
        assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
        assert(io.saves==0&&serialized(io.saved)==saved);
        assert(io.removed.size()==failAt+1);
        for(std::size_t untouched=failAt;untouched<paths.size();++untouched)
            assert(io.files.count(paths[untouched])==1);
    }
    io=populated();io.failSave=true;
    const auto saved=serialized(io.saved);
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.files==remaining&&serialized(io.saved)==saved&&io.saves==1);
    io.failSave=false;
    assert(restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(serialized(io.saved)==serialized(restart())); // Retry needs no durable fresh flag.
    io=populated();io.changeJournalAfterRemoval=true;
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.saves==0&&io.saved.plan[0].compressedBytes>record().plan[0].compressedBytes);
    io=populated();io.failReadAt=2;
    assert(!restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.saves==0&&serialized(io.saved)==serialized(record()));

    // A completed journal can be stale relative to a changed on-disk marker.
    // The caller offers fresh repair only after detecting that conflict; the
    // same target and no-downgrade rules still allow a complete forward replay.
    auto completed=record();completed.phase=CoreRecoveryPhase::Completed;
    completed.currentStep=completed.plan.size();completed.beforeVersion=completed.plan.back().version;
    io=populated();io.saved=completed;
    assert(restartCoreRecoveryWithIo(completed,restart(),io,error));
    assert(io.saves==1&&serialized(io.saved)==serialized(restart()));

    // A previously absent cache is valid; missing paths never need removing.
    io=populated();for(const auto& path:paths)io.files.erase(path);
    assert(restartCoreRecoveryWithIo(record(),restart(),io,error));
    assert(io.removed.empty()&&io.saves==1);
    // Equal content hashes share one exact cache scope even with differing
    // case, URLs or version metadata. Never remove the same path twice.
    auto duplicate=restart();duplicate.plan[1].sha256=std::string(64,'C');
    io=populated();
    assert(restartCoreRecoveryWithIo(record(),duplicate,io,error));
    assert(io.removed.size()==5&&io.saves==1);

    auto paused=record();paused.phase=CoreRecoveryPhase::BeforeDownload;
    auto equalVersion=record();equalVersion.replayFull=true;
    io=populated();io.saved=paused;
    // The old and new scopes may overlap. Even then, only the ZIPs are
    // cleared before the old journal atomically becomes a same-version replay.
    io.files[std::string(config::CacheRoot)+"/foreign-directory.zip"]=CoreRecoveryCacheFile::Other;
    assert(restartCoreRecoveryWithIo(paused,equalVersion,io,error));
    assert(io.removed==cachePaths(equalVersion)&&io.saves==1);
    assert(io.files.at(std::string(config::CacheRoot)+"/foreign-directory.zip")==CoreRecoveryCacheFile::Other);
}
}

int main() {
    validationTests();exactJournalTests();preflightTests();purgeAndCommitTests();
    std::cout<<"Fresh recovery validation, exact journal checks, scoped cache reset and failure-order tests passed\n";
}
