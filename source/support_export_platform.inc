// Support export is on-demand. SLC is read only; the completed ZIP goes to SD.
bool exportSupportArchive(const GameInstall& install,const std::string& report,
                          SupportExportResult& result,std::string& error,const ProgressCallback& progress){
    result={};error.clear();
    return runBackgroundOperation("HQ support export",progress,[&](const ProgressCallback& relay){
        const auto status=[&](const std::string& detail){if(relay)relay("Exporting support ZIP",detail,-1);};
        status("Saving the current HQ report...");
        const std::string root=config::SdRoot;
        if(!writeTextFileAtomic(root+"/diagnostics.txt",report,error))return false;
        SupportArchivePlan plan;
        if(!config::ghostsMode()){
            std::string warning;status("Recording native folder links (read-only)...");
            if(!writeNativeLinkCensus(install,warning))plan.note("Native folder census unavailable: "+warning);
        }
        for(const auto* name:{"performance.log","last-install-failure.txt","native-link-census.txt","ghosts/last-install-failure.txt"})
            addSupportLog(plan,root+"/"+name,"hq/"+std::string(name),false);
        constexpr const char* alias="unbound_support_slc";
        status("Reading Wii U crash logs from internal SLC storage...");
        const auto mountLogs=[&](){
            if(!mochaReady)return false;
            auto mounted=Mocha_MountFS(alias,"/dev/slc01","/vol/storage_slc01");
            if(mounted==MOCHA_RESULT_ALREADY_EXISTS)mounted=Mocha_MountFS(alias,nullptr,"/vol/storage_slc01");
            return mounted==MOCHA_RESULT_SUCCESS;
        };
        struct ReleaseMount {const char* alias;bool owned;~ReleaseMount(){if(owned)Mocha_UnmountFS(alias);}} mount{alias,mountLogs()};
        if(mount.owned)collectConsoleSupportLogs(plan,std::string(alias)+":/sys/logs");
        else plan.note("Console crash logs unavailable: SLC access could not be mounted.");
        const auto path=root+"/support-report.zip";
        if(!hasSpaceFor(root,plan.bytes+report.size()+1024*1024,error))return false;
        if(!writeSupportArchive(path,report,plan,error,status))return false;
        result.path=path;result.consoleLogs=plan.consoleFiles;result.otherLogs=static_cast<unsigned>(plan.files.size())-plan.consoleFiles+1;
        if(!plan.notes.empty())result.warning="See export-summary.txt for unavailable or skipped logs.";
        status("Support ZIP saved and verified.");return true;
    });
}
