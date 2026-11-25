// Included after the shared map installer in main.cpp's application namespace.
struct WorkshopContentCatalog {
    std::vector<MapPack> items;
    std::vector<std::string> versions;
    std::string notice;
    bool loaded=false,dirty=true;
    std::size_t selected=0;
    int scroll=0,scrollTarget=0;
};
WorkshopContentCatalog sessionCustomMaps,sessionMods;
bool isWorkshopContentTab(WorkshopTab tab){return tab==WorkshopTab::CustomMaps||tab==WorkshopTab::Mods;}
WorkshopContentCatalog& workshopContentCatalog(WorkshopTab tab){return tab==WorkshopTab::Mods?sessionMods:sessionCustomMaps;}

void loadWorkshopContent(Ui& ui,WorkshopTab tab,bool refresh=false){
    auto& catalog=workshopContentCatalog(tab);const bool mods=tab==WorkshopTab::Mods;
    if(catalog.loaded&&!refresh)return;
    catalog.loaded=true;catalog.items.clear();catalog.notice.clear();
    ui.loading(mods?"Checking mods":"Checking custom maps",mods?"Reading mod listings...":"Reading community map listings...",-1);
    const auto cache=std::string(config::gameCacheRoot())+(mods?"/mods.txt":"/custom-maps.txt");
    const auto parse=[&](const std::string& text,std::string& error){return parseWorkshopContent(text,catalog.items,error,mods);};
    std::string text,error;
    if(downloadText(mods?config::gameModsUrl():config::gameCustomMapsUrl(),text,error)&&parse(text,error)){
        if(!writeTextFileAtomic(cache,text,error))catalog.notice="Catalog loaded; its saved copy could not be updated.";
        return;
    }
    std::string saved;
    if(readInstallState(cache,saved,error)&&!saved.empty()&&parse(saved,error))
        catalog.notice=mods?"Using the saved mod catalog.":"Using the saved custom map catalog.";
    else{catalog.items.clear();catalog.notice=mods?"Mod catalog unavailable. Try Refresh.":"Custom map catalog unavailable. Try Refresh.";}
}

void refreshWorkshopContentVersions(const GameInstall& install,WorkshopTab tab){
    auto& catalog=workshopContentCatalog(tab);catalog.versions.assign(catalog.items.size(),{});
    if(!mapPackTargetReady(install))return;
    std::string marker,error;
    if(!readInstallState(install.update.root+"/"+config::VersionRelativePath,marker,error)){
        appendWarning(catalog.notice,"Installed content records could not be read.");return;
    }
    for(std::size_t i=0;i<catalog.items.size();++i){
        error.clear();catalog.versions[i]=installedMapVersion(install,marker,catalog.items[i].id,error);
        if(!error.empty())appendWarning(catalog.notice,"An installed content receipt could not be read.");
    }
}
