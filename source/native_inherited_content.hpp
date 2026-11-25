// Private implementation included by platform.cpp inside its anonymous namespace.
// No raw metadata flags are manufactured: public SDK/WUT FSA interfaces expose
// no supported child-reference creation API. Preserve actual BASE bytes instead.

struct NativeInheritedProgress {
    const ProgressCallback& progress;
    std::uint64_t completed=0,total=0;
    EtaState eta;
    std::uint64_t copied=0,verified=0;
    void report(const std::string& phase,const std::string& file,std::uint64_t amount=0) {
        // Percentage and ETA cover both the copy and its read-back check. The
        // visible file amount must not mistake those two passes for twice the
        // amount of data being installed or twice the required disk space.
        const auto maximum=std::numeric_limits<std::uint64_t>::max();
        const auto originalBytes=total/2;
        const auto advance=[amount](std::uint64_t& value,std::uint64_t limit){
            const auto added=std::min(amount,value<limit?limit-value:0);
            value+=added;
            return added;
        };
        auto work=amount;
        if(phase=="Copying")work=advance(copied,total?originalBytes:maximum);
        else if(phase=="Checking")work=advance(verified,total?originalBytes:maximum);
        const auto workLimit=total?total:maximum;
        completed+=std::min(work,completed<workLimit?workLimit-completed:0);
        const auto now=SDL_GetTicks();eta.observe(completed,now);
        const int percent=!total?-1:completed>=total?99:completed<=maximum/100
            ?static_cast<int>(std::min<std::uint64_t>(99,completed*100/total))
            :static_cast<int>(std::min<long double>(99,100.0L*completed/total));
        if(progress&&(amount==0||eta.shouldRender(percent,now)))
            progress("Preserving original game files",phase+(file.empty()?"":" "+leafName(file))+"\n"+
                "Copied "+dataAmount(copied)+(total?" of "+dataAmount(originalBytes):"")+" | Verified "+dataAmount(verified)+"\n"+
                eta.remaining(completed,total,now),percent);
    }
};

struct NativeInheritedTreeIo {
    NativeInheritedProgress& status;
    FileIoContext buffers;
    std::uint64_t payloadReserve=0;
    explicit NativeInheritedTreeIo(NativeInheritedProgress& report,std::uint64_t reserve=0)
        :status(report),payloadReserve(reserve){}
    InheritedStat inspect(const std::string& path) {
        struct stat information{};errno=0;
        if(lstat(path.c_str(),&information)!=0)
            return {errno==ENOENT?InheritedEntryKind::Missing:InheritedEntryKind::Error,0,errno?errno:EIO};
        if(S_ISLNK(information.st_mode))return {InheritedEntryKind::Link,0,0};
        if(S_ISDIR(information.st_mode))return {InheritedEntryKind::Directory,0,0};
        if(S_ISREG(information.st_mode)&&information.st_size>=0)
            return {InheritedEntryKind::Regular,static_cast<std::uint64_t>(information.st_size),0};
        return {InheritedEntryKind::Other,0,0};
    }
    bool listNames(const std::string& path,std::vector<std::string>& names,std::string& error) {
        names.clear();
        if(inspect(path).kind!=InheritedEntryKind::Directory){error="Original content is not a real readable directory: "+path;return false;}
        DIR* directory=opendir(path.c_str());
        if(!directory){error="Could not read original content directory: "+path;return false;}
        bool ok=true;
        for(;;){
            errno=0;const auto* entry=readdir(directory);
            if(!entry){if(errno){error="Could not finish reading original content: "+path;ok=false;}break;}
            const std::string name=entry->d_name;
            if(name=="."||name=="..")continue;
            if(names.size()>=32768){error="Original content directory exceeds the safe inventory limit: "+path;ok=false;break;}
            names.push_back(name);
        }
        if(closedir(directory)!=0){error="Could not close original content directory: "+path;ok=false;}
        return ok;
    }
    bool makeDirectory(const std::string& path,std::string& error) {
        if(!buffers.prepareDirectory(path)){error=buffers.directoryError();return false;}return true;
    }
    bool copyInheritedFile(const std::string& from,const std::string& to,std::uint64_t expected,
                           bool mayReplaceOwnedStage,std::string& error) {
        const auto fail=[&](const char* reason){error=std::string("Original content repair: ")+reason+": "+to;return false;};
        if(!buffers.ready())return fail("could not allocate copy buffers");
        const auto source=inspect(from),target=inspect(to);
        if(source.kind!=InheritedEntryKind::Regular||source.bytes!=expected)return fail("stock file changed before copying");
        if(target.kind!=InheritedEntryKind::Missing&&!(mayReplaceOwnedStage&&target.kind==InheritedEntryKind::Regular))
            return fail("existing installed file was retained");
        if(!buffers.prepareDirectory(parentPath(to)))return fail("destination folder is unavailable");
        const std::string temporary=to+".unbound-new";
        const auto scratch=inspect(temporary);
        if(scratch.kind!=InheritedEntryKind::Missing&&scratch.kind!=InheritedEntryKind::Regular)
            return fail("unexpected temporary entry was retained");
        // Only a reserved scratch file under a validated owned tree is cleared.
        if(scratch.kind==InheritedEntryKind::Regular&&std::remove(temporary.c_str())!=0)
            return fail("could not clear interrupted scratch file");
        unsigned long long available=0;
        constexpr std::uint64_t Reserve=8ULL*1024*1024;
        if(!freeSpaceForPath(to,available)||payloadReserve>available||Reserve>available-payloadReserve||
           expected>available-payloadReserve-Reserve)return fail("not enough free space for verified stock copy");
        FILE* input=std::fopen(from.c_str(),"rb");
        if(!input)return fail("could not open original file");
        FILE* output=std::fopen(temporary.c_str(),"wb");
        if(!output){std::fclose(input);return fail("could not create stock-copy scratch file");}
        bool ok=std::setvbuf(input,buffers.input.data(),_IOFBF,buffers.input.size())==0&&
                std::setvbuf(output,buffers.output.data(),_IOFBF,buffers.output.size())==0;
        Sha256 sourceHash;std::uint64_t count=0;
        status.report("Copying",from);
        while(ok){
            const auto read=std::fread(buffers.transfer.data(),1,buffers.transfer.size(),input);
            if(std::ferror(input)){ok=false;break;}
            if(!read)break;
            if(count>expected||read>expected-count){ok=false;break;}
            if(std::fwrite(buffers.transfer.data(),1,read,output)!=read){ok=false;break;}
            sourceHash.update(buffers.transfer.data(),read);count+=read;status.report("Copying",from,read);
        }
        if(count!=expected)ok=false;
        struct stat finalSource{};
        if(fstat(fileno(input),&finalSource)!=0||!S_ISREG(finalSource.st_mode)||finalSource.st_size<0||
           static_cast<std::uint64_t>(finalSource.st_size)!=expected)ok=false;
        if(ok&&std::fflush(output)!=0)ok=false;
        if(ok&&fsync(fileno(output))!=0)ok=false;
        if(std::fclose(output)!=0)ok=false;
        if(std::fclose(input)!=0)ok=false;
        if(!ok){std::remove(temporary.c_str());return fail("copy/read/close failed; installed file unchanged");}
        status.report("Checking",from);
        FileHashOptions hashOptions;hashOptions.expectedBytes=expected;
        std::uint64_t last=0;
        hashOptions.progress=[&](std::uint64_t bytes,std::uint64_t){
            const auto delta=bytes-last;last=bytes;status.report("Checking",from,delta);
        };
        std::string copiedHash;
        const auto checked=sha256FileDetailed(temporary,copiedHash,hashOptions);
        if(!checked.ok()||checked.bytes!=expected||copiedHash!=sourceHash.finish()){
            std::remove(temporary.c_str());return fail("copied stock-file integrity check failed");
        }
        const auto current=inspect(to);
        if(mayReplaceOwnedStage&&current.kind!=InheritedEntryKind::Missing&&current.kind!=InheritedEntryKind::Regular)
            return fail("staged destination changed type; unexpected entry retained");
        if(!mayReplaceOwnedStage){
            if(current.kind!=InheritedEntryKind::Missing)return fail("destination changed; existing file retained");
            if(std::rename(temporary.c_str(),to.c_str())!=0)return fail("could not commit verified stock file");
        }else if(!commitInstallReplacement(temporary,to,error))return false;
        return true;
    }
};

#ifndef UNBOUND_NATIVE_INHERITED_COPY_TEST
bool validatedInheritedTarget(const GameInstall& install,std::string& error) {
    const auto* sku=install.sku?findSkuByTitle(install.sku->updateTitleId):nullptr;
    if(!sku||sku->baseTitleId!=install.sku->baseTitleId||!install.destinationReady||!install.update.present||
       (install.update.device!=Device::MLC&&install.update.device!=Device::USB)||
       install.update.root!=stockTitleRoot(install.update.device,sku->updateTitleId)){
        error="The game-content repair destination could not be verified.";return false;
    }
    InstallLocation verified;
    if(probeStockTitle(install.update.device,sku->updateTitleId,verified,error)!=TitleProbe::Valid)return false;
    // Metadata-only migrations also write completion receipts. Validate all
    // ancestors before those writes, not just before copying a missing file.
    for(const auto& prefix:directoryPrefixes(install.update.root+"/content")){
        struct stat state{};
        if(lstat(prefix.c_str(),&state)!=0||!S_ISDIR(state.st_mode)){
            error="The inherited-content destination has an unavailable or referenced ancestor: "+prefix;
            return false;
        }
    }
    return true;
}

bool resolveInheritedSource(const GameInstall& install,std::string& content,std::uint16_t& version,std::string& error) {
    GameInstall original=install;findPhysicalBase(original,&install);
    if(original.base.present&&(original.base.device==Device::MLC||original.base.device==Device::USB)){
        InstallLocation verified;
        if(probeStockTitle(original.base.device,install.sku->baseTitleId,verified,error)!=TitleProbe::Valid)return false;
        content=verified.root+"/content";version=verified.titleVersion;return true;
    }
    alignas(64) MCPTitleListType disc{};
    std::string report="HQ "+std::string(config::AppVersion)+" disc source check\n";
    bool resolved=false;
    if(matchingDiscTitle(install.sku->baseTitleId,disc)){
        const std::string path(disc.path,strnlen(disc.path,sizeof(disc.path)));
        report+="MCP title path: "+path+"\n";
        struct DiscIo {
            std::string& report;
            bool attach(unsigned volume){
                if(volume>=DiscSourceVolumeCount)return false;
                if(discSourceMounted[volume])return true;
                const auto& source=DiscSourceVolumes[volume];
                // Attach an unlocked devoptab to an OS-mounted disc volume.
                // A null device never mounts/unmounts the physical disc volume.
                const auto status=Mocha_MountFS(source.alias,nullptr,source.root);
                report+="Attach "+std::string(source.root)+": "+std::to_string(status)+"\n";
                discSourceMounted[volume]=status==MOCHA_RESULT_SUCCESS;
                return discSourceMounted[volume];
            }
            bool read(const std::string& path,std::string& text){
                const bool ok=readTextFile(path,text,64*1024);
                report+="Metadata "+path+": "+(ok?"read":"unavailable")+"\n";return ok;
            }
            bool directory(const std::string& path){struct stat state{};
                return lstat(path.c_str(),&state)==0&&S_ISDIR(state.st_mode);}
        } io{report};
        resolved=resolveDiscSource(path,install.sku->baseTitleId,content,version,io);
    }else report+="Matching game disc not reported by MCP.\n";
    report+="Result: "+std::string(resolved?content:"unavailable")+"\n";
    std::string ignored;writeTextFileAtomic(std::string(config::SdRoot)+"/disc-source-check.txt",report,ignored);
    if(resolved)return true;
    error="HQ could not read the original files from the matching game disc. Insert the correct Black Ops II "
        "disc, wait for the Wii U Menu to recognize it, then reopen HQ and retry. "
        "Your saved download was kept and no original reference was replaced. "
        "Details: disc-source-check.txt in the HQ SD folder.";return false;
}

struct NativeInheritedBatch {
    const GameInstall& install;
    const ProgressCallback& progress;
    NativeInheritedProgress status;
    NativeInheritedTreeIo trees;
    std::string sourceContent;
    std::uint16_t sourceVersion=0;
    std::map<std::string,InheritedTreePlan> plans;
    std::uint64_t payloadReserve=0;
    bool spaceChecked=false;
    NativeInheritedBatch(const GameInstall& selected,const ProgressCallback& report,std::uint64_t reserve=0)
        :install(selected),progress(report),status{report,0,0,{}},trees(status,reserve),payloadReserve(reserve){}
    bool source(std::string& error){return !sourceContent.empty()||resolveInheritedSource(install,sourceContent,sourceVersion,error);}
    bool plan(const FolderLinkPaths& paths,bool live,std::string& error){
        if(plans.count(paths.target))return true;
        const auto contentRoot=install.update.root+"/content";
        std::set<std::string> scope;
        if(paths.target==contentRoot||!addContentFolderLinkPrefixes(contentRoot,paths.target,scope,error)||
           !scope.count(paths.target)){error="An inherited repair path is outside verified content.";return false;}
        if(!source(error))return false;
        const auto relative=paths.target.substr(contentRoot.size()+1);
        if(progress)progress("Checking original game files","Calculating preserved content and available space...",-1);
        InheritedTreePlan prepared;
        if(!buildInheritedTreePlan(sourceContent+"/"+relative,live?paths.target:paths.staged(),
            live?InheritedCopyMode::RepairMissing:InheritedCopyMode::FreshStage,trees,prepared,error))return false;
        std::size_t entries=prepared.items.size();
        std::uint64_t sourceTotal=prepared.sourceBytes,copyTotal=prepared.copyBytes;
        constexpr std::uint64_t MaximumBatchBytes=64ULL*1024*1024*1024;
        for(const auto& existing:plans){
            if(existing.second.items.size()>65536-std::min<std::size_t>(entries,65536)||
               !inherited_detail::add(sourceTotal,existing.second.sourceBytes)||
               !inherited_detail::add(copyTotal,existing.second.copyBytes)){
                error="The inherited-content batch exceeds its safe inventory limit.";return false;
            }
            entries+=existing.second.items.size();
        }
        if(entries>65536||sourceTotal>MaximumBatchBytes||copyTotal>MaximumBatchBytes){
            error="The inherited-content batch exceeds its safe entry or byte limit.";return false;
        }
        plans.emplace(paths.target,std::move(prepared));return true;
    }
    std::vector<InheritedTreePlan> uniquePlans(std::string& error){
        std::vector<InheritedTreePlan> unique;
        for(const auto& item:plans)if(!addInheritedTreePlan(unique,item.second,error))return {};
        return unique;
    }
    bool checkSpace(std::string& error){
        if(spaceChecked)return true;
        auto unique=uniquePlans(error);if(!error.empty())return false;
        std::uint64_t required=0;
        if(payloadReserve>std::numeric_limits<std::uint64_t>::max()-8ULL*1024*1024||
           !inheritedRequiredSpace(unique,payloadReserve+8ULL*1024*1024,required,error))return false;
        unsigned long long available=0;
        if(!freeSpaceForPath(install.update.root,available)){error="Could not determine free space for original-content preservation.";return false;}
        if(required>available){error="Not enough free space to preserve original content. Required: "+
            std::to_string(required)+" bytes; available: "+std::to_string(available)+" bytes.";return false;}
        status.total=0;
        for(const auto& item:unique){
            if(item.copyBytes>(std::numeric_limits<std::uint64_t>::max()-status.total)/2){error="Stock copy progress size overflow.";return false;}
            status.total+=item.copyBytes*2;
        }
        if(progress)progress("Preserving original game files","Local copy: "+dataAmount(status.total/2)+
            "\nRequired free space: "+dataAmount(required)+"; available: "+dataAmount(available),-1);
        spaceChecked=true;return true;
    }
    std::string completion(const FolderLinkPaths& paths,const InheritedTreePlan& plan) {
        Sha256 inventory;
        for(const auto& item:plan.items){const auto row=item.relative+"|"+(item.directory?"D":"F")+"|"+std::to_string(item.bytes)+"\n";
            inventory.update(row.data(),row.size());}
        return "HQINHERITED2\ntarget="+paths.target+"\nsource="+plan.sourceRoot+"\nversion="+
            std::to_string(sourceVersion)+"\ninventory="+inventory.finish()+"\n";
    }
    bool complete(const FolderLinkPaths& paths,bool& done,std::string& error){
        done=false;
        const auto found=plans.find(paths.target);
        if(found==plans.end()){error="Inherited content plan was not prepared.";return false;}
        const auto receipt=paths.target+".unbound-link-complete";
        const auto state=trees.inspect(receipt);
        if(state.kind==InheritedEntryKind::Missing)return true;
        if(state.kind!=InheritedEntryKind::Regular||state.bytes>2048){error="Invalid inherited-content completion record was retained: "+receipt;return false;}
        std::string text;
        if(!readTextFile(receipt,text,2048)){error="Could not read inherited-content completion record: "+receipt;return false;}
        done=found->second.copyBytes==0&&text==completion(paths,found->second);return true;
    }
};
#endif // UNBOUND_NATIVE_INHERITED_COPY_TEST
