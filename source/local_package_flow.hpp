// Included after the normal installer; the browser owns its controller/touch loop.
struct LocalPackageBrowserResult { int tab=-1,section=-1;bool exit=false;std::uint32_t held=0; };
LocalPackageBrowserResult openLocalPackages(Ui& ui,const GameInstall& install,bool contentAllowed){
    TouchBoundary boundary(ui);std::vector<SdPackageEntry> entries;std::string folder,notice;
    std::size_t selected=0;int scroll=0;bool redraw=true;NavigationRepeat repeat;
    const ProgressCallback checking=[&](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    const auto browse=[&](const std::string& path){
        std::vector<SdPackageEntry> found;std::string error;
        if(!listSdPackages(path,found,error)){notice=error;redraw=true;return false;}
        folder=path;entries=std::move(found);selected=0;scroll=0;notice.clear();redraw=true;return true;
    };
    refreshLocalPackageRecovery();
    std::string initial=std::string(SdBrowseRoot)+(config::ghostsMode()?"/unbound-ghosts/packages":"/unbound/packages");
    if(localPackageRepairPending&&localPackageWarning.empty())initial=localPackagePending.package.path.substr(0,localPackagePending.package.path.find_last_of('/'));
    if(!browse(initial)&&!browse(SdBrowseRoot))notice="SD card unavailable. Insert it, then choose Refresh.";
    while(WHBProcIsRunning()){
        if(redraw){ui.localPackages(folder.empty()?SdBrowseRoot:folder,entries,selected,scroll,notice,localPackageRepairPending);redraw=false;}
        std::optional<TouchAction> touch;const auto input=readUiInput(ui,&touch);
        auto buttons=input.buttons|repeat.update(input.buttons,input.held,SDL_GetTicks());
        if(buttons&button::Plus)return {-1,-1,true};
        if(touch&&touch->kind==TouchActionKind::Sidebar)return {-1,touch->value,false};
        if(touch&&touch->kind==TouchActionKind::WorkshopTab&&touch->value>=0&&touch->value<int(WorkshopTab::CustomPackages))return {touch->value,-1,false};
        if(buttons&button::L)return {-1,2,false};
        if(buttons&button::Left)return {int(WorkshopTab::Mods),-1,false,input.held|button::Left};
        if(buttons&(button::B|button::Y)){
            if(!folder.empty()&&folder!=SdBrowseRoot)browse(folder.substr(0,folder.find_last_of('/')));
            else if(buttons&button::B)return {-1,2,false};
            continue;
        }
        if(buttons&button::X){browse(folder.empty()?SdBrowseRoot:folder);continue;}
        if(touch&&touch->kind==TouchActionKind::PackageEntry&&touch->value>=0&&std::size_t(touch->value)<entries.size()){
            selected=std::size_t(touch->value);buttons=button::A;redraw=true;
        }
        if((buttons&button::Down)&&selected+1<entries.size()){++selected;redraw=true;}
        if((buttons&button::Up)&&selected){--selected;redraw=true;}
        const int top=234+int(selected)*66-scroll;
        if(top<234){scroll=std::max(0,scroll-(234-top));redraw=true;}
        if(top>576){scroll=std::min(std::max(0,int(entries.size())*66-408),scroll+top-576);redraw=true;}
        if((buttons&button::A)&&selected<entries.size()){
            const auto entry=entries[selected];
            if(entry.directory){browse(entry.path);continue;}
            if(!contentAllowed){waitModal(ui,"HQ update required","Update HQ before installing a local ZIP.",true);redraw=true;continue;}
            if(mapRepairPending||inheritedRepairGate.blocked()||recoveryStatus!=CoreRecoveryLoadStatus::Missing){
                waitModal(ui,"Finish the current installation","Open Install / Update and finish the current installation before starting a local package.",true);redraw=true;continue;
            }
            if(localPackageRepairPending&&!localPackageWarning.empty()){
                waitModal(ui,"Local package recovery needs attention",localPackageWarning,true);redraw=true;continue;
            }
            LocalPackageInfo info;std::string error;
            if(!runBackgroundOperation("Inspect local ZIP",checking,[&](const ProgressCallback& progress){return inspectLocalPackage(entry.path,install,info,error,progress);})){waitModal(ui,"ZIP could not be reviewed",error,true);redraw=true;continue;}
            if(localPackageRepairPending&&(!sameLocalPackageTarget(localPackagePending,install)||!sameLocalPackageSource(localPackagePending,info))){
                waitModal(ui,"Resume the original package","Select "+localPackagePending.package.path+" with the same game, storage and language used when its installation started. Other packages are blocked until it finishes.",true);redraw=true;continue;
            }
            std::string summary=entry.name+"\n\n";
            if(info.updateFiles)summary+="Game update: "+std::to_string(info.updateFiles)+" files / "+packageBytes(info.updateBytes)+"\n"+install.update.root+"\n\n";
            if(info.sdFiles)summary+="SD content: "+std::to_string(info.sdFiles)+" files / "+packageBytes(info.sdBytes)+"\n"+config::gameSdContentDisplayPath()+"\n\n";
            summary+="Included files will be replaced. The source ZIP stays on your SD card. Official release version numbers are kept.";
            if(!confirmAction(ui,localPackageRepairPending?"Resume custom package?":"Install custom package?",summary,"INSTALL")){redraw=true;continue;}
            const auto journal=localPackageJournal(info,install);LocalPackagePending checked;
            if(!parseLocalPackageJournal(journal,checked,error)){waitModal(ui,"Cannot save package recovery",error,true);redraw=true;continue;}
            bool armed=false;
            const auto beforeMutation=[&](std::string& failure){if(armed)return true;armed=writeTextFileAtomic(localPackageStatePath(),journal,failure);return armed;};
            const ProgressCallback installing=[&](const std::string& title,const std::string& detail,int percent){ui.installLoading(title,detail,percent);};
            bool ok=installLocalPackage(info,install,error,installing,beforeMutation);
            if(ok)ok=writeTextFileAtomic(localPackageStatePath(),"",error);
            refreshLocalPackageRecovery();
            if(ok){notice=entry.name+" installed.";waitModal(ui,"Custom package installed","The included files are installed. Your source ZIP was kept.");}
            else{lastSessionError=error;waitModal(ui,"Custom package install stopped",error+(armed?"\n\nRetry this exact ZIP before launching the game.":"\n\nNo new installation was marked complete."),true);}
            redraw=true;
        }
        waitFrame(ui);
    }
    return {-1,-1,true};
}
