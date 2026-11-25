// Included inside main.cpp's application namespace after the advanced installer.
void estimateQuickUpdates(QuickUpdatePlan& plan){
    std::string log;UpdateTimingHistory history;
    if(readTextFile(std::string(config::SdRoot)+"/performance.log",log,64*1024))history.read(log);
    const double installRate=plan.target.update.device==Device::USB?history.usb:history.nand;
    const auto saved=[&](const Release& release){
        std::vector<std::string> paths=sdPackageCandidates(release.downloadUrl,selectedSdPackageRoot());
        paths.push_back(coreRecoveryArchivePath(release));
        for(const auto& path:paths){struct stat st{};if(lstat(path.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>0&&
            static_cast<unsigned long long>(st.st_size)==release.compressedBytes)return true;}
        return false;
    };
    std::size_t index=0;
    const auto fill=[&](QuickUpdateRow& row,std::uint64_t network){
        row.networkBytes=network;
        row.downloadSeconds=UpdateTimingHistory::seconds(network,history.download,1024*1024);
        row.installSeconds=UpdateTimingHistory::seconds(row.bytes,installRate,2*1024*1024);
        row.timingFromHistory=(!network||history.download>0)&&installRate>0;
    };
    if(!plan.core.empty()){
        std::uint64_t network=0;for(const auto* release:plan.core)if(!saved(*release))network+=release->compressedBytes;
        fill(plan.rows[index++],network);
    }
    for(const auto& map:plan.maps)fill(plan.rows[index++],saved(map.release)?0:map.release.compressedBytes);
}
bool collectQuickUpdates(Ui& ui,const GameInstall& install,const InstallPreferences& saved,
                         const std::vector<Release>& releases,bool contentAllowed,bool settingsLoaded,
                         QuickUpdatePlan& plan,std::string& error){
    plan={};error.clear();
    if(!quickUpdateEligible(install,saved,settingsLoaded,contentAllowed,
                            repairNeeded()||recoveryStatus!=CoreRecoveryLoadStatus::Missing))return true;
    loadMapCatalog(ui);
    std::string marker;
    if(!readInstallState(install.update.root+"/"+config::VersionRelativePath,marker,error))return false;
    std::vector<QuickMapState> maps;
    for(const auto& pack:sessionMapCatalog){
        auto version=installedMapVersion(install,marker,pack.id,error);
        if(!error.empty())return false;
        if(version.empty())continue;
        maps.push_back({&pack,version});
    }
    plan=quickUpdatePlan(releases,install,maps,sdCoreVersion);
    estimateQuickUpdates(plan);
    return true;
}

InstallMenuResult runQuickUpdates(Ui& ui,GameInstall& install,GameInstall& automatic,
                                 InstallPreferences& saved,const std::vector<Release>& releases,
                                 bool contentAllowed,bool& settingsLoaded,QuickUpdatePlan plan,
                                 int* requestedSection){
    TouchBoundary boundary(ui);
    if(requestedSection)*requestedSection=-1;
    int scroll=0;bool redraw=true;TouchScroll swipe;NavigationRepeat navigation;
    const ProgressCallback checking=[&ui](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    while(WHBProcIsRunning()){
        if(redraw){ui.setNavigation(1,false);ui.quickUpdate(install,plan.rows,plan.coreCurrent,scroll);redraw=false;}
        std::optional<TouchAction> touch;const auto input=readUiInput(ui,&touch);
        const auto buttons=input.buttons|navigation.update(input.buttons,input.held,SDL_GetTicks());
        if(buttons&button::Plus)return InstallMenuResult::Exit;
        if(buttons&button::B)return InstallMenuResult::Back;
        if(input.buttons||input.held)swipe.reset();
        else if(swipe.update(input.touch,SDL_GetTicks(),quickUpdateMaximumScroll(plan.rows.size()),scroll,{164,247,952,229})){
            ui.cancelTouchGesture();redraw=true;
        }
        int delta=0;if(buttons&button::Down)delta=88;else if(buttons&button::Up)delta=-88;
        const auto next=std::clamp(scroll+delta,0,quickUpdateMaximumScroll(plan.rows.size()));
        if(next!=scroll){scroll=next;redraw=true;}
        if(buttons&button::X){
            std::string refreshError;
            const auto result=runInstallMenu(ui,install,automatic,saved,releases,contentAllowed,settingsLoaded,requestedSection);
            if(result!=InstallMenuResult::Back||(requestedSection&&*requestedSection>=0))return result;
            if(!collectQuickUpdates(ui,install,saved,releases,contentAllowed,settingsLoaded,plan,refreshError)){
                waitModal(ui,"Update details unavailable",refreshError,true);return InstallMenuResult::Back;
            }
            if(plan.empty())return InstallMenuResult::Completed;
            scroll=0;redraw=true;continue;
        }
        if(buttons&button::A){
            GameInstall validated=install;std::string error;
            if(!runBackgroundOperation("Validate update destination",checking,[&](const ProgressCallback& relay){
                return applyInstallPreferences(validated,saved,error,relay,false);
            })){
                install=validated;
                waitModal(ui,"Update location unavailable",error+"\n\nNo update was started. Reconnect the saved storage or use Install settings.",true);
                return InstallMenuResult::Back;
            }
            refreshRecovery(true);
            std::vector<MapJob> pendingMaps;
            if(!readMapPending(validated,pendingMaps,error)||!pendingMaps.empty())mapRepairPending=true;
            if(!refreshInheritedRepair(ui,validated)||repairNeeded()||recoveryStatus!=CoreRecoveryLoadStatus::Missing){
                install=validated;
                waitModal(ui,"Finish the existing installation",unfinishedSummary()+"\n\nOpen Install settings to finish its exact saved installation before starting another update.",true);
                return runInstallMenu(ui,install,automatic,saved,releases,contentAllowed,settingsLoaded,requestedSection);
            }
            if(!quickUpdateEligible(validated,saved,settingsLoaded,contentAllowed,false)){
                install=validated;
                const auto official=officialUpdateWarning(install);
                waitModal(ui,"Review the installation",official.empty()?"The saved installation needs attention before updating. No update was started.":official,true);
                return runInstallMenu(ui,install,automatic,saved,releases,contentAllowed,settingsLoaded,requestedSection);
            }
            QuickUpdatePlan fresh;
            if(!collectQuickUpdates(ui,validated,saved,releases,contentAllowed,settingsLoaded,fresh,error)){
                waitModal(ui,"Update details unavailable",error+"\n\nNo update was started.",true);return InstallMenuResult::Back;
            }
            if(!sameQuickUpdatePlan(plan,fresh)){
                install=validated;plan=std::move(fresh);scroll=0;redraw=true;
                if(plan.empty()){
                    waitModal(ui,"No updates needed","Your installed content is already current. No files were changed.");return InstallMenuResult::Completed;
                }
                waitModal(ui,"Update details changed","The installation or selected packages changed. Review the refreshed destination and updates before continuing.");
                continue;
            }
            install=validated;
            downloadWasCancelled=false;
            const auto started=SDL_GetTicks();
            const bool previousInternetSource=useInternetPackageSource;useInternetPackageSource=false;
            bool ok=plan.core.empty()||installCorePlan(ui,install,plan.core,error,false,true);
            if(ok)ok=finishContentMigration(ui,install,error);
            if(ok)ok=installMapJobs(ui,install,plan.maps,error,true);
            useInternetPackageSource=previousInternetSource;
            if(ok){
                lastSessionError.clear();refreshRecovery();
                const auto seconds=(SDL_GetTicks()-started)/1000;
                waitModal(ui,"Update complete","Your installed content is up to date.\n\nCompleted in "+
                    std::to_string(seconds/60)+" min "+std::to_string(seconds%60)+" sec.\n\n"+
                    (pluginRestartRequired?"Restart the console/Aroma before playing.":"Press A to return to HQ."));
                return InstallMenuResult::Completed;
            }
            refreshRecovery();
            if(downloadWasCancelled){
                waitModal(ui,"Update paused","Your safe download and installation progress were kept. Reopen Install / Update when you are ready to continue.");
            }else{
                lastSessionError=error;
                std::string ignored;
                const auto detail="HQ "+std::string(config::AppVersion)+" / r"+std::to_string(config::AppReleaseRevision)+
                    "\nDestination: "+install.update.root+"\nLanguage: "+install.languageFolder+"\n"+error;
                writeTextFileAtomic(config::gameInstallFailurePath(),detail.substr(0,DiagnosticsMaximumInstallFailureBytes),ignored);
                const auto feedback=installFailureFeedback(error);waitModal(ui,feedback.title,feedback.message,true);
            }
            return InstallMenuResult::Back;
        }
        waitFrame(ui);
    }
    return InstallMenuResult::Back;
}

InstallMenuResult openContentMenu(Ui& ui,GameInstall& install,GameInstall& automatic,
                                  InstallPreferences& saved,const std::vector<Release>& releases,
                                  bool contentAllowed,bool& settingsLoaded,int* requestedSection=nullptr){
    QuickUpdatePlan plan;std::string error;
    if(collectQuickUpdates(ui,install,saved,releases,contentAllowed,settingsLoaded,plan,error)&&!plan.empty())
        return runQuickUpdates(ui,install,automatic,saved,releases,contentAllowed,settingsLoaded,std::move(plan),requestedSection);
    if(!error.empty())waitModal(ui,"Update details unavailable",error,true);
    return runInstallMenu(ui,install,automatic,saved,releases,contentAllowed,settingsLoaded,requestedSection);
}
