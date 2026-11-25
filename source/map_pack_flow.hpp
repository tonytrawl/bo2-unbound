// Included inside main.cpp's application namespace, after the Core installer.
using MapJob = QuickMapUpdate;
std::string mapPendingPath(){return std::string(config::gameStateRoot())+"/maps/pending.txt";}

bool readMapPending(const GameInstall& install,std::vector<MapJob>& jobs,std::string& error){
    std::string text;
    if(!readInstallState(mapPendingPath(),text,error)){
        mapRepairPending=true;
        return false;
    }
    mapRepairPending=false;
    if(text.empty())return true;
    std::istringstream in(text);std::string target,record;std::getline(in,target);std::getline(in,record);
    const auto fields=packFields(target);
    // Older HQ builds could leave a journal for an SD installation.
    if(fields.size()==3||(fields.size()==4&&fields[3]=="SD"))return true;
    mapRepairPending=true;
    std::vector<std::string> errors;const auto releases=parseReleases(record,&errors);
    if(fields.size()!=4||!packId(fields[2])||!errors.empty()||releases.size()!=1||
       (fields[3]!="USB"&&fields[3]!="NAND")){
        error="The unfinished map-pack record could not be read. Export a Support report before changing it.";
        return false;
    }
    if(!mapPackTargetReady(install)||fields[0]!=install.update.root||
       fields[1]!=install.languageFolder||fields[3]!=mapStorageTag(install.update.device)){
        error="Reconnect "+fields[0]+" and select "+fields[1]+" in Install settings to finish the saved map pack.";
        return false;
    }
    jobs.push_back({fields[2],"Resume "+fields[2],releases[0]});
    return true;
}

std::vector<MapPack> sessionMapCatalog;
std::string sessionMapNotice;
bool sessionMapCatalogLoaded=false;
void loadMapCatalog(Ui& ui){
    if(sessionMapCatalogLoaded)return;
    sessionMapCatalogLoaded=true;
    std::string text,error;
    auto& packs=sessionMapCatalog;
    auto& warning=sessionMapNotice;
    ui.loading("Checking map packs","Reading the optional content catalog...",-1);
    const auto cache=std::string(config::gameCacheRoot())+"/map-packs.txt";
    if(fetchSharedManifest(text,error,config::activeGame)&&parseMapPacks(text,packs,error)){
        std::string cacheError;writeTextFileAtomic(cache,text,cacheError);
    }else{
        warning="Map catalog offline. Core can still be installed.";
        std::string cached;
        if(readInstallState(cache,cached,error)&&!cached.empty()&&parseMapPacks(cached,packs,error))warning="Using the saved map catalog.";
        else packs.clear();
    }
}
std::string installedMapVersion(const GameInstall& install,const std::string& marker,
                                const std::string& id,std::string& error){
    const auto device=install.update.device;
    const auto version=packVersion(marker,id,install.languageFolder,device);
    if(version.empty())return {};
    std::string receipt;
    const auto stateRoot=std::string(config::gameStateRoot())+"/maps";
    if(!readInstallState(mapReceiptPath(stateRoot,id,install.languageFolder,device),receipt,error))return {};
    return receipt==version+"\n"+install.update.root+"\n"?version:std::string{};
}

unsigned installedMapUpdateCount(Ui& ui,const GameInstall& install){
    if(!mapPackTargetReady(install))return 0;
    std::string marker,error;
    if(!readInstallState(install.update.root+"/"+config::VersionRelativePath,marker,error))return 0;
    loadMapCatalog(ui);unsigned count=0;
    for(const auto& pack:sessionMapCatalog){
        auto version=installedMapVersion(install,marker,pack.id,error);
        if(!version.empty()&&mapPackRelease(pack,version))++count;
    }
    return count;
}

bool chooseMapPacks(Ui& ui,const GameInstall& install,std::vector<MapJob>& jobs,bool reinstallSelected=false){
    TouchBoundary boundary(ui);jobs.clear();std::string error;
    if(!readMapPending(install,jobs,error)){waitModal(ui,"Map installation needs attention",error,true);return false;}
    if(!jobs.empty())return confirmAction(ui,"Resume map installation?",
        "Finish "+jobs.front().id+" on "+deviceName(install.update.device)+
        " before choosing more content. Its verified saved ZIP will be reused.","RESUME");
    loadMapCatalog(ui);
    const auto& packs=sessionMapCatalog;
    const auto& warning=sessionMapNotice;
    std::string marker,preferences;
    if(!readInstallState(install.update.root+"/"+config::VersionRelativePath,marker,error)||
       !readInstallState(std::string(config::gameStateRoot())+"/maps/selection.txt",preferences,error)){
        waitModal(ui,"Could not read content selection",error,true);return false;
    }
    std::vector<std::string> versions;
    std::vector<bool> selected;
    for(const auto& p:packs){
        auto v=installedMapVersion(install,marker,p.id,error);
        if(!error.empty()){waitModal(ui,"Could not read pack record",error,true);return false;}
        versions.push_back(v);
        selected.push_back(mapPackSelected(preferences,p.id));
    }
    int focused=0;bool redraw=true,reinstall=reinstallSelected;NavigationRepeat repeat;
    while(WHBProcIsRunning()){
        if(redraw){ui.mapPacks(packs,selected,versions,focused,install,warning.empty()?"Unchecking a pack keeps its files and pauses future updates.":warning,reinstall);redraw=false;}
        std::optional<TouchAction> touch;const auto input=readUiInput(ui,&touch);
        auto b=input.buttons|repeat.update(input.buttons,input.held,SDL_GetTicks());
        if(touch&&touch->kind==TouchActionKind::MapPackRow){focused=touch->value;b|=button::A;redraw=true;}
        if((b&button::Right)||(touch&&touch->kind==TouchActionKind::MapPackDetails)){
            const int details=touch&&touch->kind==TouchActionKind::MapPackDetails?touch->value:focused;
            if(details>=0&&details<int(packs.size()))waitModal(ui,packs[details].name,packs[details].description);
            redraw=true;continue;
        }
        if(b&(button::B|button::Plus))return false;
        if(b&button::X){reinstall=!reinstall;redraw=true;}
        const int focusCount=int(packs.size())+(packs.empty()?1:2);
        if(b&button::Up){focused=(focused+focusCount-1)%focusCount;redraw=true;}
        if(b&button::Down){focused=(focused+1)%focusCount;redraw=true;}
        if(b&button::A){
            if(focused<int(packs.size())){selected[focused]=!selected[focused];redraw=true;}
            else if(focused==int(packs.size())+1){reinstall=!reinstall;redraw=true;}
            else{
                std::string choice;
                for(std::size_t i=0;i<packs.size();++i){
                    choice+=packs[i].id+"|"+(selected[i]?"1":"0")+"\n";
                    if(selected[i])if(const auto* r=mapPackRelease(packs[i],versions[i],reinstall))jobs.push_back({packs[i].id,packs[i].name,*r});
                }
                if(!packs.empty()&&!writeTextFileAtomic(std::string(config::gameStateRoot())+"/maps/selection.txt",choice,error)){
                    jobs.clear();waitModal(ui,"Selection could not be saved",error,true);redraw=true;continue;
                }
                return true;
            }
        }
        waitFrame(ui);
    }
    return false;
}

bool confirmContentQueue(Ui& ui,const GameInstall& install,const std::vector<const Release*>& core,const std::vector<MapJob>& maps){
    TouchBoundary boundary(ui);std::vector<std::pair<std::string,std::string>> rows;std::uint64_t bytes=0;
    if(core.empty())rows.push_back({"Unbound Core","Unchanged / not queued"});
    for(const auto* r:core){rows.push_back({"Unbound Core "+r->version,packageBytes(r->compressedBytes)+" / "+deviceName(install.update.device)});bytes+=r->compressedBytes;}
    for(const auto& job:maps){rows.push_back({job.name+" "+job.release.version,packageBytes(job.release.compressedBytes)+" / "+deviceName(install.update.device)});bytes+=job.release.compressedBytes;}
    int scroll=0;bool redraw=true;TouchScroll swipe;
    while(WHBProcIsRunning()){
        if(redraw){ui.contentQueue(rows,install,packageBytes(bytes),scroll);redraw=false;}
        const auto input=readUiInput(ui);
        if(input.buttons||input.held)swipe.reset();
        else if(swipe.update(input.touch,SDL_GetTicks(),std::max(0,int(rows.size())*80-Ui::ContentQueueViewport.h+16),scroll,{Ui::ContentQueueViewport.x,Ui::ContentQueueViewport.y,Ui::ContentQueueViewport.w,Ui::ContentQueueViewport.h})){
            ui.cancelTouchGesture();redraw=true;
        }
        if(input.buttons&button::A)return true;
        if(input.buttons&(button::B|button::Plus))return false;
        int delta=0;
        if(input.buttons&button::Down)delta=80;else if(input.held&button::Down)delta=8;
        if(input.buttons&button::Up)delta=-80;else if(input.held&button::Up)delta=-8;
        const auto next=std::clamp(scroll+delta,0,std::max(0,int(rows.size())*80-Ui::ContentQueueViewport.h+16));
        if(next!=scroll){scroll=next;redraw=true;}waitFrame(ui);
    }
    return false;
}

bool installMapJobs(Ui& ui,const GameInstall& install,const std::vector<MapJob>& jobs,std::string& error,bool automaticLocalSource=false){
    if(jobs.empty())return true;
    if(!mapPackTargetReady(install)){error="Reconnect the selected game update storage and retry.";return false;}
    if(install.modVersion.empty()||install.modVersion=="Not installed"||
       (!sdCoreVersion.empty()&&compareVersions(install.modVersion,sdCoreVersion)<0)){
        error="Install the compatible Core release before map packs.";return false;
    }
    for(const auto& job:jobs){
        const auto& r=job.release;
        std::string archive=coreRecoveryArchivePath(r);
        const auto cached=archive;
        for(const auto& path:sdPackageCandidates(r.downloadUrl,selectedSdPackageRoot())){
            struct stat st{};if(lstat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))continue;
            if(!automaticLocalSource&&!confirmAction(ui,"Map pack found on SD",job.name+"\n\nVerify and install this ZIP from your SD card? B uses the internet.","USE SD PACKAGE"))break;
            DownloadControl control;DownloadInputContext input{ui,control};control.setInputPoll(pollDownloadInput,&input);
            const auto result=checkDownloadedPackage(path,r.sha256,r.compressedBytes,error,
                [&](const std::string&,const std::string& detail,int percent){ui.loading("Verifying "+job.name,detail,percent,true);},&control);
            if(result==PackageVerificationStatus::Valid){archive=path;break;}
            if(result==PackageVerificationStatus::Cancelled){downloadWasCancelled=true;return false;}
            if(!automaticLocalSource&&!confirmAction(ui,"SD ZIP does not match","Your ZIP was kept. Download the verified map package instead?","DOWNLOAD")){downloadWasCancelled=true;return false;}
            error.clear();break;
        }
        if(archive==cached&&!getPackage(ui,job.name,r.downloadUrl,r.sha256,archive,error,r.compressedBytes))return false;
        const auto journal=install.update.root+"|"+install.languageFolder+"|"+job.id+"|"+mapStorageTag(install.update.device)+"\n"+releaseRecord(r)+"\n";
        bool armed=false;
        const auto beforeMutation=[&](std::string& failure){
            if(armed)return true;
            if(!writeTextFileAtomic(mapPendingPath(),journal,failure))return false;
            mapRepairPending=true;armed=true;return true;
        };
        const auto progress=[&](const std::string& stage,const std::string& detail,int percent){ui.installLoading(stage+": "+job.name,detail,percent);};
        if(!installMapArchive(archive,install,job.id,r.version,error,progress,beforeMutation))return false;
        if(!writeTextFileAtomic(mapPendingPath(),"",error))return false;
        mapRepairPending=false;
        if(archive==cached)std::remove(archive.c_str());
    }
    return true;
}

bool finishContentMigration(Ui& ui,const GameInstall& install,std::string& error){
    if(config::ghostsMode()||sdCoreVersion.empty()||compareVersions(install.modVersion,sdCoreVersion)<0)return true;
    bool changed=false;
    const auto progress=[&](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    const bool ok=runBackgroundOperation("Finishing Core setup",progress,[&](const ProgressCallback&){return retireModLink(changed,error);});
    if(changed){pluginRestartRequired=true;retiredPluginRestartRequired=true;}
    if(!ok){retiredPluginRestartRequired=true;pluginRestartRequired=true;}
    return ok;
}
