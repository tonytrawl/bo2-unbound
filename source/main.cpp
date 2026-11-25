#include "map_packs.hpp"
#include "config.hpp"
#include "sd_package.hpp"
#include "touch_scroll.hpp"
#include "network_stats.hpp"
#include "globe_motion.hpp"
#include "diagnostics.hpp"
#include "input.hpp"
#include "model.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include "ui.hpp"
#include "background_operation.hpp"
#include "install_selection.hpp"
#include "core_recovery.hpp"
#include "navigation_repeat.hpp"
#include "dashboard_navigation.hpp"
#include "touch_routes.hpp"
#include "plugin_policy.hpp"
#include "install_feedback.hpp"
#include "inherited_repair_gate.hpp"
#include "custom_maps.hpp"
#include "local_package_state.hpp"
#include "update_estimates.hpp"

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <nn/act/client_cpp.h>
#include <sysapp/launch.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/proc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_set>
#include <vector>

namespace {
using namespace unbound;

constexpr const char* BuiltInChanges =
    "WELCOME TO BLACK OPS 2 UNBOUND HQ\n\n"
    "Open Install / Update to choose USB or internal storage and a detected "
    "language folder. Save your selection for future launches, or Reset to Auto. "
    "An unavailable saved destination never silently switches to another drive. "
    "Game-content installs no longer copy old files to SD backups. "
    "Packages remain verified, and update.txt is written only after each "
    "package finishes. Unfinished installs keep their exact package chain and "
    "verified download for repair after restarting HQ. Press B during a download "
    "to pause safely; unpacking and installation must finish without interruption. "
    "Hold the D-pad to navigate lists and scroll full plugin or mod descriptions. "
    "HQ and plugin updater recovery is unchanged. Open Support & Stats with Y "
    "(Wii Remote 2) to view information and export a report to your SD card. "
    "Official game-update warnings and download resume remain available. "
    "Protarium is available in Plugin Workshop with optional startup updates.";

constexpr const char* BuiltInPluginCatalog =
    "# UNBOUND_PLUGIN_CATALOG_V1\n"
    "PLUGIN|GAMEPAD_MIC_REDIRECT|GamePad Mic Redirect|Tony (tonytrawl)|Redirects the Wii U GamePad built-in microphone for headset-only voice input. Experimental; v0.4.0 adds mute and push-to-talk controls.|"
    "https://downloads.unc-bo2.fun/release-manifest.txt|"
    "https://github.com/Unbound-Project/gamepad_mic_redirect|"
    "wiiu/environments/aroma/plugins/GamePadMicRedirect.wps\n"
    "PLUGIN|PROTARIUM|Protarium|Protarium Network|Connect compatible Wii U games to Protarium Network. Installs the paired Inkay Aroma module and plugin. A full console restart is required; Inkay changes network and SSL behavior while active.|"
    "https://downloads.unc-bo2.fun/release-manifest.txt|"
    "https://github.com/Protarium-Network/Inkay-GitHub-Release|"
    "wiiu/environments/aroma/modules/Inkay-pretendo.wms,wiiu/environments/aroma/plugins/Inkay-pretendo.wps\n";

enum class Screen { Home, News, LocalMods, Catalog, Details, Support, Network };
std::string lastSessionError;
CoreRecoveryRecord recoveryRecord;
CoreRecoveryLoadStatus recoveryStatus=CoreRecoveryLoadStatus::Missing;
std::string recoveryWarning;
bool downloadWasCancelled=false;
bool pluginRestartRequired=false;
bool mapRepairPending=false;
bool localPackageRepairPending=false;
LocalPackagePending localPackagePending;
std::string localPackageWarning;
bool retiredPluginRestartRequired=false;
std::string sdCoreVersion;
InheritedRepairGate inheritedRepairGate;

// UI gestures and VPAD reads stay on this thread. A physical controller owns
// its sample, so a held finger cannot also activate a second control.
AppInput readUiInput(Ui& ui,std::optional<TouchAction>* selectedAction=nullptr){
    if(selectedAction)selectedAction->reset();
    auto input=readInput();
    if(input.buttons||input.held)ui.cancelTouchGesture();
    else if(const auto action=ui.touchAction(input.touch)){
        if(action->kind==TouchActionKind::Button)input.buttons|=touchButtonMask(*action);
        else if(selectedAction)*selectedAction=action;
    }
    input.buttons=dismissBeforeActivate(input.buttons);
    const bool supplyRunWasEnabled=ui.supplyRunEnabled();
    ui.waitingGameInput(input.buttons,input.held,input.touch);
    if(ui.supplyRunEnabled()!=supplyRunWasEnabled){
        std::string saveError;
        if(!writeTextFileAtomic(config::SupplyRunSettingsPath,ui.supplyRunEnabled()?"on\n":"off\n",saveError))
            lastSessionError="Supply Run preference could not be saved: "+saveError;
    }
    return input;
}

struct TouchBoundary {
    Ui& ui;
    explicit TouchBoundary(Ui& target):ui(target){ui.invalidateTouch();}
    ~TouchBoundary(){ui.invalidateTouch();}
};

struct DownloadInputContext { Ui& ui; DownloadControl& control; };
void pollDownloadInput(void* opaque){
    auto& context=*static_cast<DownloadInputContext*>(opaque);
    const auto input=readUiInput(context.ui);
    if(input.buttons&(button::B|button::Plus))context.control.requestCancel();
    if(!context.control.cancelRequested())context.ui.animateDownloadWaiting();
}

void refreshRecovery(bool allowWrites=false){
    recoveryWarning.clear();
    recoveryStatus=loadCoreRecovery(recoveryRecord,recoveryWarning,allowWrites);
}
std::string localPackageStatePath(){return std::string(config::gameStateRoot())+"/local-package-install.txt";}
void refreshLocalPackageRecovery(){
    std::string text;localPackageWarning.clear();localPackagePending={};
    if(!readInstallState(localPackageStatePath(),text,localPackageWarning))localPackageRepairPending=true;
    else if(text.empty())localPackageRepairPending=false;
    else{localPackageRepairPending=true;parseLocalPackageJournal(text,localPackagePending,localPackageWarning);}
}

bool repairNeeded(){
    return localPackageRepairPending||mapRepairPending||inheritedRepairGate.blocked()||recoveryStatus==CoreRecoveryLoadStatus::Invalid||
        (recoveryStatus==CoreRecoveryLoadStatus::Pending&&coreRecoveryNeedsRepair(recoveryRecord));
}

std::string unfinishedSummary(){
    if(localPackageRepairPending)return "A custom package needs to finish. Open Workshop > Custom Packages and retry its original ZIP.";
    if(mapRepairPending)return "A map pack needs to finish installing. Open Install / Update to resume.";
    if(inheritedRepairGate.blocked())return "Original game files need restoration. Open Install / Update before launching the game.";
    if(recoveryStatus==CoreRecoveryLoadStatus::Invalid)return "The saved install record needs attention. Open Install / Update for details.";
    if(recoveryStatus!=CoreRecoveryLoadStatus::Pending)return {};
    return std::string(recoveryRecord.phase==CoreRecoveryPhase::Installing?"Repair needed":"Unfinished installation")+
        ": "+deviceName(recoveryRecord.device)+" / "+recoveryRecord.language+
        ". Select its destination to resume the saved package chain.";
}

void downloadScreen(Ui& ui,DownloadControl& control,const std::string& title,
                    const std::string& detail,int percent){
    const auto input=readUiInput(ui);
    if(input.buttons&(button::B|button::Plus))control.requestCancel();
    ui.downloadLoading(control.cancelRequested()?"Stopping download safely":title,
        control.cancelRequested()?"Finishing buffered SD writes and saving any resumable progress.":detail,
        percent,!control.cancelRequested());
}

void waitFrame(Ui& ui){ui.pump();OSSleepTicks(OSMillisecondsToTicks(16));}
void finishFrame(Ui& ui,std::uint32_t started){ui.pump();const std::uint32_t elapsed=SDL_GetTicks()-started;if(elapsed<16)OSSleepTicks(OSMillisecondsToTicks(16-elapsed));}
void waitModal(Ui& ui,const std::string& title,const std::string& body,bool error=false){
    TouchBoundary boundary(ui);
    if(error)lastSessionError=title+": "+body;
    int scroll=0;int maximum=ui.modal(title,body,error,"CONTINUE",scroll);
    while(WHBProcIsRunning()){
        const auto input=readUiInput(ui);
        if(input.buttons&(button::A|button::B|button::Plus))break;
        int delta=0;
        if(input.buttons&button::Down)delta=48;else if(input.held&button::Down)delta=8;
        if(input.buttons&button::Up)delta=-48;else if(input.held&button::Up)delta=-8;
        const int next=std::clamp(scroll+delta,0,maximum);
        if(next!=scroll){scroll=next;maximum=ui.modal(title,body,error,"CONTINUE",scroll);}
        waitFrame(ui);
    }
}
bool confirmAction(Ui& ui,const std::string& title,const std::string& body,const std::string& action,bool error=false){
    TouchBoundary boundary(ui);
    if(error)lastSessionError=title+": "+body;
    int scroll=0;int maximum=ui.modal(title,body,error,action,scroll,true);
    while(WHBProcIsRunning()){
        const auto input=readUiInput(ui);
        if(input.buttons&(button::B|button::Plus))return false;
        if(input.buttons&button::A)return true;
        int delta=0;
        if(input.buttons&button::Down)delta=48;else if(input.held&button::Down)delta=8;
        if(input.buttons&button::Up)delta=-48;else if(input.held&button::Up)delta=-8;
        const int next=std::clamp(scroll+delta,0,maximum);
        if(next!=scroll){scroll=next;maximum=ui.modal(title,body,error,action,scroll,true);}
        waitFrame(ui);
    }
    return false;
}

int pluginCatalogMaximumScroll(std::size_t count){return std::max(0,int(count)*142-16-408);}
int localModsMaximumScroll(std::size_t count){return std::max(0,int(count)*82-390);}

bool placeholderServer(){const std::string url=config::gameReleasesUrl();return url.find("example.com")!=std::string::npos||url.find("YOUR_GITHUB_NAME")!=std::string::npos;}
std::string cachedChanges(){std::string cached;if(readTextFile(config::gameChangesCachePath(),cached,64*1024)&&!trim(cached).empty())return cached;if(config::ghostsMode())return "Ghosts updates are unavailable right now.";if(readTextFile(config::LegacyChangesCachePath,cached,64*1024)&&!trim(cached).empty())return cached;return BuiltInChanges;}
bool parseReleaseText(const std::string& text,std::vector<Release>& releases,std::string& error){
    std::vector<std::string> parseErrors;releases=parseReleases(text,&parseErrors);
    if(!parseErrors.empty()){error=parseErrors.front();releases.clear();return false;}
    if(releases.empty()){error="The release feed contains no installable releases";return false;}
    return true;
}
void appendWarning(std::string& warning,const std::string& value){if(value.empty())return;if(!warning.empty())warning+="\n\n";warning+=value;}
bool cacheTextOnlyWhenChanged(const std::string& path,const std::string& value,std::size_t maximum,bool& changed,std::string& error){
    if(value.size()>maximum){changed=false;error="Cache content exceeds its safety limit";return false;}
    std::string existing;
    if(readTextFile(path,existing,maximum)&&existing==value){changed=false;return true;}
    changed=true;return writeTextFileAtomic(path,value,error);
}
std::vector<NewsImage> cacheNewsImages(Ui& ui,const std::string& changes,std::string& warning,bool& downloadedArtwork){
    downloadedArtwork=false;
    auto parsed=parseNewsImages(changes);std::vector<NewsImage> result;std::vector<std::string> activePaths;std::unordered_set<std::string> seen;
    for(auto image:parsed){
        if(!seen.insert(image.url).second)continue;
        Sha256 hash;hash.update(image.url.data(),image.url.size());
        image.cachePath=std::string(config::gameCacheRoot())+"/news-"+hash.finish().substr(0,24)+".img";
        activePaths.push_back(image.cachePath);result.push_back(image);
    }
    unsigned long long freed=0;std::string cleanupError;
    if(!cleanupDownloadCache(activePaths,freed,cleanupError))appendWarning(warning,cleanupError);
    unsigned index=0,failures=0;
    for(auto& image:result){
        struct stat info{};
        const bool cached=stat(image.cachePath.c_str(),&info)==0&&info.st_size>0&&
            static_cast<unsigned long long>(info.st_size)<=config::MaxNewsImageBytes;
        if(!cached){
            std::remove((image.cachePath+".bmp").c_str());
            ui.loading("Loading update artwork",image.caption.empty()?"Downloading embedded image...":image.caption,
                       result.empty()?0:int((index*100)/result.size()));
            std::string imageError;
            if(!downloadFile(image.url,image.cachePath,imageError,{},config::MaxNewsImageBytes)){
                image.cachePath.clear();++failures;
            }else downloadedArtwork=true;
        }
        ++index;
    }
    if(failures)appendWarning(warning,std::to_string(failures)+
        " embedded update image(s) could not be downloaded. Text content is still available.");
    return result;
}
// One successful fetch per game. HQ self-updates and global Aroma plugins
// always use the BO2 manifest, even while the Ghosts dashboard is selected.
bool fetchSharedManifest(std::string& text,std::string& error,Game game=Game::BlackOpsII){
    struct Manifest { bool downloaded=false; std::string text,error; };
    static std::array<Manifest,2> manifests;
    auto& manifest=manifests[game==Game::Ghosts?1:0];
    if(!manifest.downloaded)
        manifest.downloaded=downloadText(game==Game::Ghosts?config::GhostsReleasesUrl:config::ReleasesUrl,
                                         manifest.text,manifest.error);
    text=manifest.text;error=manifest.error;return manifest.downloaded;
}

bool fetchCatalog(Ui& ui,const std::string* prefetchedReleaseText,std::vector<Release>& releases,std::string& changes,std::string& updateStatus,std::string& warning,bool& releasesChanged,bool& changesChanged){
    releasesChanged=false;changesChanged=false;
    warning.clear();
    sdCoreVersion.clear();
    if(placeholderServer()){changes=cachedChanges();updateStatus="Release server is not configured";warning=updateStatus;return false;}
    std::string releaseText,releaseError;
    ui.loading("Checking for updates","Reading the universal release feed...",10);
    bool releaseDownloaded=false;
    if(prefetchedReleaseText){releaseText=*prefetchedReleaseText;releaseDownloaded=true;}
    else releaseDownloaded=fetchSharedManifest(releaseText,releaseError,config::activeGame);
    if(releaseDownloaded&&parseReleaseText(releaseText,releases,releaseError)){
        std::string cacheError;if(!cacheTextOnlyWhenChanged(config::gameReleasesCachePath(),releaseText,config::MaxManifestBytes,releasesChanged,cacheError))appendWarning(warning,"Could not cache release data: "+cacheError);
        sdCoreVersion=nativeSdCoreVersion(releaseText);
        updateStatus=releasesChanged?"Latest release information loaded":"Release information is unchanged";
    }else{
        std::string cached;
        if(readTextFile(config::gameReleasesCachePath(),cached,config::MaxManifestBytes)&&parseReleaseText(cached,releases,releaseError)){updateStatus="Using cached release information";sdCoreVersion=nativeSdCoreVersion(cached);}
        else{releases.clear();updateStatus="Could not reach the release server";appendWarning(warning,"Release feed: "+releaseError);}
    }
    std::string changesError;
    ui.loading("Loading Unbound HQ","Fetching What's New...",40);
    if(!downloadText(config::gameChangesUrl(),changes,changesError)){
        changes=cachedChanges();appendWarning(warning,"HQ news: "+changesError);
    }else{
        std::string cacheError;if(!changes.empty()&&!cacheTextOnlyWhenChanged(config::gameChangesCachePath(),changes,64*1024,changesChanged,cacheError))appendWarning(warning,"Could not cache HQ news: "+cacheError);
    }
    return !releases.empty();
}

struct PluginRuntime {
    WorkshopPlugin plugin;
    PluginRelease release;
    PluginInstallStatus status;
    bool releaseReady = false;
    bool autoUpdateEnabled = false;
    bool checked = false;
    std::string feedStatus;
};

std::string lowerKey(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char character){return static_cast<char>(std::tolower(character));});
    return value;
}

bool isProtariumPlugin(const WorkshopPlugin& plugin){
    if(lowerKey(plugin.key)=="protarium")return true;
    for(const auto& path:plugin.managedPaths){
        const auto normalized=lowerKey(path);
        if(normalized=="wiiu/environments/aroma/modules/inkay-pretendo.wms"||
           normalized=="wiiu/environments/aroma/plugins/inkay-pretendo.wps")return true;
    }
    return false;
}

bool parsePluginFeedText(const std::string& text,const WorkshopPlugin& plugin,
                         PluginRelease& release,std::string& error){
    std::vector<std::string> parseErrors;
    if(!parsePluginRelease(text,plugin,release,&parseErrors)){
        error=parseErrors.empty()?"Plugin release feed is invalid":parseErrors.front();
        return false;
    }
    if(release.compressedBytes>config::MaxPluginArchiveBytes){
        error="Plugin release exceeds HQ's package-size limit";
        return false;
    }
    return true;
}

bool fetchTrustedPluginRelease(const WorkshopPlugin& plugin,PluginRelease& release,
                               std::string& status,std::string& error,
                               const std::string* prefetchedText=nullptr){
    release={};status.clear();error.clear();
    const std::string cachePath=std::string(config::PluginCacheRoot)+"/"+lowerKey(plugin.key)+"-release.txt";
    std::string cachedText,cachedError;PluginRelease cachedRelease;bool cachedReady=false;
    if(readTextFile(cachePath,cachedText,config::MaxManifestBytes))
        cachedReady=parsePluginFeedText(cachedText,plugin,cachedRelease,cachedError);

    std::string remoteText,remoteError;PluginRelease remoteRelease;
    const bool downloaded=prefetchedText
        ? (remoteText=*prefetchedText,true)
        : fetchSharedManifest(remoteText,remoteError);
    const bool remoteReady=downloaded&&parsePluginFeedText(remoteText,plugin,remoteRelease,remoteError);
    if(!remoteReady){
        if(cachedReady){release=cachedRelease;status="Using cached release data; publisher feed: "+remoteError;return true;}
        error=remoteError.empty()?"No valid plugin release feed is available":remoteError;
        status="Release feed unavailable: "+error;return false;
    }
    if(cachedReady&&remoteRelease.revision==cachedRelease.revision&&
       !samePluginRelease(remoteRelease,cachedRelease)){
        release=cachedRelease;
        status="Publisher reused revision "+std::to_string(remoteRelease.revision)+
               " with different metadata; HQ retained the cached record.";
        return true;
    }
    if(cachedReady&&remoteRelease.revision<cachedRelease.revision){
        release=cachedRelease;
        status="Publisher feed rollback detected; HQ retained cached revision "+
               std::to_string(cachedRelease.revision)+".";
        return true;
    }
    release=remoteRelease;bool changed=false;std::string cacheError;
    if(!cacheTextOnlyWhenChanged(cachePath,remoteText,config::MaxManifestBytes,changed,cacheError))
        status="Release ready, but it could not be cached: "+cacheError;
    else status=changed?"Latest verified release metadata loaded.":"Release metadata is unchanged and ready.";
    return true;
}

std::vector<WorkshopPlugin> builtInPlugins(){
    std::vector<std::string> errors;
    return parsePluginCatalog(BuiltInPluginCatalog,&errors);
}

bool loadWorkshopCatalog(Ui& ui,std::vector<WorkshopPlugin>& plugins,std::string& warning,
                         bool requestRefresh){
    if(requestRefresh)
        ui.loading("Refreshing Plugin Workshop","Checking for new curated listings...",15);
    warning.clear();plugins.clear();
    const auto ensureRequiredBuiltIns=[](std::vector<WorkshopPlugin>& parsed){
        const auto builtIns=builtInPlugins();
        std::vector<WorkshopPlugin> merged;
        merged.reserve(parsed.size()+3);
        for(const char* key:{"GAMEPAD_MIC_REDIRECT","PROTARIUM"}){
            const auto required=std::find_if(builtIns.begin(),builtIns.end(),[&](const WorkshopPlugin& plugin){
                return plugin.key==key;
            });
            if(required!=builtIns.end())merged.push_back(*required);
        }
        for(auto& plugin:parsed){
            const auto key=lowerKey(plugin.key);
            const bool required=key=="unbound_mod_link"||
                                std::find(plugin.managedPaths.begin(),plugin.managedPaths.end(),"wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps")!=plugin.managedPaths.end()||
                                key=="gamepad_mic_redirect"||
                                isProtariumPlugin(plugin);
            if(!required)merged.push_back(std::move(plugin));
        }
        parsed=std::move(merged);
    };
    auto parseCatalog=[&](const std::string& text,std::string& parseError){
        std::vector<std::string> errors;auto parsed=parsePluginCatalog(text,&errors);
        if(!errors.empty()||parsed.empty()){
            parseError=errors.empty()?"The catalog contains no plugin listings":errors.front();
            return std::vector<WorkshopPlugin>{};
        }
        return parsed;
    };
    std::string remote,remoteError;
    if(requestRefresh&&downloadText(config::WorkshopUrl,remote,remoteError)){
        plugins=parseCatalog(remote,remoteError);
        if(!plugins.empty()){
            ensureRequiredBuiltIns(plugins);
            bool changed=false;std::string cacheError;
            if(!cacheTextOnlyWhenChanged(config::WorkshopCachePath,remote,config::MaxManifestBytes,changed,cacheError))
                warning="Workshop catalog could not be cached: "+cacheError;
            else warning=changed?"Catalog refreshed. Open a plugin to check its release.":
                                 "Catalog is unchanged. Open a plugin to check its release.";
            return true;
        }
    }
    std::string cached;
    if(readTextFile(config::WorkshopCachePath,cached,config::MaxManifestBytes)){
        std::string cachedError;plugins=parseCatalog(cached,cachedError);
        if(!plugins.empty()){
            ensureRequiredBuiltIns(plugins);
            warning=requestRefresh
                ? "Could not refresh the catalog; showing saved listings. "+remoteError
                : "Showing saved listings. Press X to refresh the catalog.";
            return true;
        }
    }
    plugins=builtInPlugins();
    warning=requestRefresh
        ? "Could not refresh the catalog; showing HQ's included listings. "+remoteError
        : "Showing included listings. Press X to check for new plugins.";
    return !plugins.empty();
}

void refreshPluginRuntime(Ui& ui,PluginRuntime& runtime,int progressPercent,
                          bool allowRecoveryWrites,
                          const std::string* prefetchedReleaseText=nullptr){
    ui.loading("Checking "+runtime.plugin.name,"Loading its latest release and install state...",progressPercent);
    std::string settingWarning;
    if(runtime.plugin.key=="UNBOUND_MOD_LINK")runtime.autoUpdateEnabled=true;
    else loadPluginAutoUpdateSetting(runtime.plugin.key,runtime.autoUpdateEnabled,settingWarning,
                                     allowRecoveryWrites);
    std::string feedError;
    runtime.releaseReady=fetchTrustedPluginRelease(runtime.plugin,runtime.release,
                                                   runtime.feedStatus,feedError,
                                                   prefetchedReleaseText);
    std::string inspectWarning;
    inspectPluginInstall(runtime.plugin,runtime.releaseReady?&runtime.release:nullptr,
                         runtime.status,inspectWarning,allowRecoveryWrites);
    runtime.checked=true;
    if(!settingWarning.empty())runtime.feedStatus+=(runtime.feedStatus.empty()?"":"\n")+settingWarning;
    if(!inspectWarning.empty())runtime.feedStatus+=(runtime.feedStatus.empty()?"":"\n")+inspectWarning;
}

bool openWorkshop(Ui& ui,std::vector<PluginRuntime>& runtimes,std::string& warning,
                  bool requestRefresh){
    std::vector<WorkshopPlugin> plugins;
    if(!loadWorkshopCatalog(ui,plugins,warning,requestRefresh))return false;
    runtimes.clear();runtimes.reserve(plugins.size());
    for(auto& plugin:plugins){
        PluginRuntime runtime;runtime.plugin=std::move(plugin);
        runtimes.push_back(std::move(runtime));
    }
    return true;
}

bool downloadAndInstallPlugin(Ui& ui,PluginRuntime& runtime,bool replaceUnmanaged,
                              std::string& error){
    error.clear();
    downloadWasCancelled=false;
    if(!runtime.releaseReady){error="No verified release is available for this plugin";return false;}
    const std::string archive=std::string(config::PluginCacheRoot)+"/"+
        lowerKey(runtime.plugin.key)+"-r"+std::to_string(runtime.release.revision)+".zip";
    ui.downloadLoading("Downloading "+runtime.plugin.name,"Starting the verified plugin download...",0,true);
    DownloadControl control;
    DownloadInputContext inputContext{ui,control};
    control.setInputPoll(pollDownloadInput,&inputContext);
    ProgressCallback transfer=[&ui,&runtime,&control](const std::string&,const std::string& detail,int percent){
        downloadScreen(ui,control,"Downloading "+runtime.plugin.name,detail,percent);
    };
    if(!downloadFile(runtime.release.downloadUrl,archive,error,transfer,
                     runtime.release.compressedBytes,&runtime.release.sha256,&control,
                     runtime.release.compressedBytes)){
        downloadWasCancelled=control.cancelled();return false;
    }
    struct stat info{};
    if(stat(archive.c_str(),&info)!=0||!S_ISREG(info.st_mode)||
       static_cast<unsigned long long>(info.st_size)!=runtime.release.compressedBytes){
        std::remove(archive.c_str());error="The verified plugin download did not match its declared size";return false;
    }
    ui.installLoading("Installing "+runtime.plugin.name,"Preparing a verified Aroma install...",0);
    ProgressCallback installProgress=[&ui,&runtime](const std::string&,const std::string& detail,int percent){
        ui.installLoading("Installing "+runtime.plugin.name,detail,percent);
    };
    const bool installerReportedSuccess=installPluginArchive(
        archive,runtime.plugin,runtime.release,replaceUnmanaged,error,installProgress);
    if(installerReportedSuccess)pluginRestartRequired=true;
    std::string inspectWarning;
    const bool inspectionSucceeded=inspectPluginInstall(
        runtime.plugin,&runtime.release,runtime.status,inspectWarning);
    if(!inspectWarning.empty())runtime.feedStatus+=(runtime.feedStatus.empty()?"":"\n")+inspectWarning;
    const bool committedCurrent=inspectionSucceeded&&
        runtime.status.presence==PluginPresence::Managed&&
        runtime.status.installedRevision==runtime.release.revision;
    if(committedCurrent){
        pluginRestartRequired=true;
        std::remove(archive.c_str());
        return true;
    }
    if(error.empty()){
        error=installerReportedSuccess
            ? "The plugin files were written, but post-install verification did not reach a clean managed state. The verified package was retained for recovery."
            : "The plugin installation did not reach the requested managed revision. The verified package was retained for recovery.";
    }
    if(!inspectWarning.empty())error+="\n"+inspectWarning;
    return false;
}
bool getPackage(Ui& ui,const std::string& label,const std::string& url,const std::string& hash,const std::string& path,std::string& error,unsigned long long expectedBytes=0){
    downloadWasCancelled=false;
    if(url.find("example.invalid")!=std::string::npos){error="This is a demonstration listing. Add a real package URL and SHA-256 to the server manifest before downloading.";return false;}
    if(hash.size()!=64){error="Manifest is missing a valid SHA-256";return false;}
    struct stat cached{};
    if(stat(path.c_str(),&cached)==0){
        DownloadControl checkingControl;
        DownloadInputContext checkingInput{ui,checkingControl};
        checkingControl.setInputPoll(pollDownloadInput,&checkingInput);
        const ProgressCallback checking=[&](const std::string&,const std::string& detail,int percent){
            ui.loading(checkingControl.cancelRequested()?"Stopping verification safely":"Checking saved package",
                checkingControl.cancelRequested()?"Closing the read handle. The saved ZIP will be retained.":detail,
                percent,!checkingControl.cancelRequested(),"Cancel verification");
        };
        std::string cachedError;
        const auto checked=checkDownloadedPackage(path,hash,expectedBytes,cachedError,checking,&checkingControl);
        if(checked==PackageVerificationStatus::Valid){ui.loading("Package ready","The saved ZIP passed verification. No new download is needed.",100);return true;}
        if(checked==PackageVerificationStatus::Cancelled){downloadWasCancelled=true;error=cachedError;return false;}
        if(checked==PackageVerificationStatus::MetadataMismatch){error=cachedError;return false;}
        if(checked==PackageVerificationStatus::ReadFailure){
            error=cachedError+"\n\nThe saved ZIP was retained. No new download was started. Check SD access and retry.";return false;
        }
        if(!confirmAction(ui,"Download needs replacing",
            "The saved package did not pass its integrity check.\n\nPress A to download a fresh copy. HQ will clear this package's rejected data first.","DOWNLOAD FRESH",true)){
            downloadWasCancelled=true;error="Fresh download cancelled. The saved package and repair record were retained.";return false;
        }
        const std::string expectedPath=std::string(config::gameCacheRoot())+"/core-"+lowerKey(hash)+".zip";
        if(path!=expectedPath){error="The failed package is outside the approved core download cache. Nothing was cleared.";return false;}
        if(!resetCorePackageDownloadCache(hash,error))return false;
    }else if(errno!=ENOENT){
        error="Could not inspect the saved package on SD: "+std::string(std::strerror(errno))+". No new download was started.";return false;
    }
    DownloadControl control;
    ui.downloadLoading("Downloading "+label,"Starting the package transfer...",0,true);
    DownloadInputContext inputContext{ui,control};
    control.setInputPoll(pollDownloadInput,&inputContext);
    ProgressCallback transfer=[&ui,&label,&control](const std::string&,const std::string& detail,int percent){downloadScreen(ui,control,"Downloading "+label,detail,percent);};
    if(!downloadFile(url,path,error,transfer,config::MaxArchiveBytes,&hash,&control,expectedBytes)){
        downloadWasCancelled=control.cancelled();return false;
    }
    ui.loading("Package verified","The streamed SHA-256 integrity check passed.",100);return true;
}
std::string packageBytes(unsigned long long bytes){
    std::ostringstream stream;stream<<std::fixed<<std::setprecision(bytes>=1024ULL*1024ULL?1:0);
    if(bytes>=1024ULL*1024ULL)stream<<double(bytes)/(1024.0*1024.0)<<" MiB";
    else if(bytes>=1024ULL)stream<<double(bytes)/1024.0<<" KiB";
    else stream<<bytes<<" bytes";
    return stream.str();
}
const char* selectedSdPackageRoot(){
    return config::ghostsMode()?GhostsSdPackageRoot:SdPackageRoot;
}
std::string planDescription(const std::vector<const Release*>& plan){
    unsigned long long bytes=0;for(const auto* release:plan){if(release->compressedBytes>~bytes)return "Download size unavailable";bytes+=release->compressedBytes;}
    return "Packages: "+std::to_string(plan.size())+"\nPackage size: "+packageBytes(bytes)+"\n\nHQ checks sd:"+std::string(selectedSdPackageRoot()+sizeof("fs:/vol/external01")-1)+" for a predownloaded ZIP before using the internet.";
}
bool refreshInheritedRepair(Ui& ui,const GameInstall& install,InheritedContentRepairInfo* result=nullptr){
    if(config::ghostsMode()||!install.sku||!install.update.present){inheritedRepairGate.clear();if(result)*result={};return true;}
    const ProgressCallback progress=[&ui](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    InheritedContentRepairInfo info;std::string checkError;
    const bool inspected=inspectInheritedContentRepair(install,info,checkError,progress);
    inheritedRepairGate.observe(install.update.root,inspected,info.needed,inspected?info.summary:checkError);
    if(result)*result=std::move(info);
    if(!inspected)lastSessionError="Original content check: "+checkError;
    return inspected;
}
enum class InheritedRepairOutcome{NotNeeded,Repaired,Blocked};
InheritedRepairOutcome ensureInheritedRepair(Ui& ui,const GameInstall& install,bool contentAllowed){
    InheritedContentRepairInfo info;
    if(!refreshInheritedRepair(ui,install,&info)){
        const auto raw=inheritedRepairGate.detail();
        waitModal(ui,"Original files need attention","HQ could not check the original files needed by this installation. Connect the original game drive or insert its disc, then retry.\n\nDetails are available in Support > Export report.",true);
        lastSessionError="Original content check: "+raw;
        return InheritedRepairOutcome::Blocked;
    }
    if(!info.needed)return InheritedRepairOutcome::NotNeeded;
    if(!contentAllowed){waitModal(ui,"HQ update required","Restart HQ and finish its app update before restoring game files.",true);return InheritedRepairOutcome::Blocked;}
    if(info.requiredBytes>info.freeBytes){
        waitModal(ui,"More game-drive space needed","Restoring original files needs "+packageBytes(info.requiredBytes)+" of free space. Available: "+packageBytes(info.freeBytes)+".\n\nFree space on the selected game drive, then retry Install / Update. Existing files were kept.",true);
        return InheritedRepairOutcome::Blocked;
    }
    std::string body;
    if(info.restoreFolders){
        body+="Put the game's original movie folder back. Unbound movies stay installed in your language folder.";
        if(info.reclaimBytes)body+="\nCopied original movies to remove: up to "+packageBytes(info.reclaimBytes)+".";
        body+="\n\n";
    }
    if(info.copyBytes||!info.restoreFolders){
        body+="Restore missing original files from your installed game before continuing. Existing mod files are kept.\n\n";
        body+="Local copy: "+packageBytes(info.copyBytes)+".";
        body+="\nFree space needed: "+packageBytes(info.requiredBytes)+"\nAvailable on game drive: "+packageBytes(info.freeBytes)+"\n\n";
    }
    body+="Keep the game closed until this finishes.";
    if(!confirmAction(ui,"Restore original game files?",body,"RESTORE FILES"))return InheritedRepairOutcome::Blocked;
    std::string repairError;
    const ProgressCallback progress=[&ui](const std::string& title,const std::string& detail,int percent){ui.installLoading(title,detail,percent);};
    if(!repairInheritedContent(install,repairError,progress)){
        inheritedRepairGate.observe(install.update.root,false,true,repairError);
        std::string ignored;
        writeTextFileAtomic(config::gameInstallFailurePath(),("HQ "+std::string(config::AppVersion)+" original content repair\n"+repairError).substr(0,DiagnosticsMaximumInstallFailureBytes),ignored);
        waitModal(ui,"Original files could not be restored","HQ kept the repair state and existing mod files. Check free space and the original game drive, then retry Install / Update before playing.\n\nDetails: Support > Export report.",true);
        lastSessionError="Original content repair: "+repairError;
        return InheritedRepairOutcome::Blocked;
    }
    if(!refreshInheritedRepair(ui,install)||inheritedRepairGate.blocked()){
        waitModal(ui,"Restoration needs attention","HQ could not confirm that restoration finished. Keep the game closed and export a Support report.",true);
        return InheritedRepairOutcome::Blocked;
    }
    lastSessionError.clear();
    return InheritedRepairOutcome::Repaired;
}
enum class AppUpdateOutcome { Current, Installed, Declined, Failed, CheckUnavailable };
AppUpdateOutcome checkForApplicationUpdate(Ui& ui,std::string& sharedReleaseFeed,
                                            std::string& warning,std::string& error){
    warning.clear();error.clear();
    sharedReleaseFeed.clear();
    // downloadFile validates a saved partial against the exact release URL/hash
    // before resuming it. Fully verified staging remains available for recovery.
    ui.loading("Checking Unbound HQ","Checking the HQ application version...",5);

    std::string cachedFeed;
    const bool cacheAvailable=readTextFile(config::ReleasesCachePath,cachedFeed,
                                           config::MaxManifestBytes);
    std::vector<std::string> cachedErrors;
    const auto cachedReleases=cacheAvailable
        ? parseAppReleases(cachedFeed,&cachedErrors) : std::vector<AppRelease>{};
    const AppRelease* cachedLatest=cachedErrors.empty()
        ? latestAppRelease(cachedReleases) : nullptr;
    auto mergeBounded=[&](const std::string& base,const AppRelease* retained,
                          std::string& output){
        if(replaceAppReleaseDirectives(base,retained,config::MaxManifestBytes,output))
            return true;
        if(retained&&replaceAppReleaseDirectives("",retained,config::MaxManifestBytes,output)){
            appendWarning(warning,
                "The shared release feed reached its size limit; only the trusted HQ revision was retained and game-content listings are unavailable.");
            return true;
        }
        output.clear();error="The trusted HQ directive exceeds the release-feed safety limit";
        return false;
    };

    std::string remoteFeed,remoteError;
    const bool remoteAvailable=fetchSharedManifest(remoteFeed,remoteError);
    if(!remoteAvailable){
        warning="HQ application update check: "+remoteError;
        sharedReleaseFeed=cacheAvailable?cachedFeed:std::string{};
    }else{
        std::vector<std::string> remoteErrors;
        const auto remoteReleases=parseAppReleases(remoteFeed,&remoteErrors);
        const auto* remoteLatest=latestAppRelease(remoteReleases);
        if(!remoteErrors.empty()){
            // Never persist malformed remote directives. Keep the content records and
            // the last unambiguous cached HQ record, but block all game writes.
            if(!mergeBounded(remoteFeed,cachedLatest,sharedReleaseFeed))
                return AppUpdateOutcome::Failed;
            error=remoteErrors.front()+
                ". No application record was selected from the malformed remote HQ feed";
            return AppUpdateOutcome::Failed;
        }
        if(cachedLatest&&remoteLatest&&cachedLatest->revision==remoteLatest->revision&&
           !sameAppRelease(*cachedLatest,*remoteLatest)){
            if(!mergeBounded(remoteFeed,cachedLatest,sharedReleaseFeed))
                return AppUpdateOutcome::Failed;
            error="HQ revision "+std::to_string(remoteLatest->revision)+
                " was reused with different release metadata. The known cached record was retained";
            return AppUpdateOutcome::Failed;
        }
        const AppRelease* retained=remoteLatest;
        if(cachedLatest&&(!retained||cachedLatest->revision>retained->revision)){
            retained=cachedLatest;
            warning=remoteLatest
                ? "HQ application feed rollback detected; retaining cached revision "+
                    std::to_string(cachedLatest->revision)
                : "The remote feed omitted its HQ directive; retaining cached revision "+
                    std::to_string(cachedLatest->revision);
        }
        if(!mergeBounded(remoteFeed,retained,sharedReleaseFeed))
            return AppUpdateOutcome::Failed;
    }

    std::vector<std::string> parseErrors;
    const auto appReleases=parseAppReleases(sharedReleaseFeed,&parseErrors);
    if(!parseErrors.empty()){
        error=parseErrors.front()+". No application record was selected from the cached HQ feed";
        return AppUpdateOutcome::Failed;
    }
    const auto* latest=latestAppRelease(appReleases);
    if(!latest){
        error="The shared release feed contains no #HQ application directive";
        appendWarning(warning,"HQ application update check: "+error);
        return AppUpdateOutcome::CheckUnavailable;
    }
    if(remoteAvailable){
        // Persist the highest trusted directive before prompting so a power loss,
        // server rollback, or later omission cannot make HQ forget it. Never
        // replace a valid shared cache with malformed content records.
        std::string retentionFeed=sharedReleaseFeed;
        std::vector<Release> contentRecords;std::string contentError;
        if(!parseReleaseText(retentionFeed,contentRecords,contentError)){
            contentRecords.clear();contentError.clear();
            if(cacheAvailable&&parseReleaseText(cachedFeed,contentRecords,contentError)){
                if(!mergeBounded(cachedFeed,latest,retentionFeed))
                    return AppUpdateOutcome::Failed;
            }else if(!replaceAppReleaseDirectives("",latest,config::MaxManifestBytes,
                                                  retentionFeed)){
                error="The trusted HQ directive exceeds the release-feed safety limit";
                return AppUpdateOutcome::Failed;
            }
        }
        if(!retentionFeed.empty()){
            bool cacheChanged=false;std::string cacheError;
            if(!cacheTextOnlyWhenChanged(config::ReleasesCachePath,retentionFeed,
                                         config::MaxManifestBytes,cacheChanged,cacheError))
                appendWarning(warning,"Could not retain the latest HQ revision: "+cacheError);
        }
    }
    if(latest->revision<=config::AppReleaseRevision){
        // A staged WUHB is useful only while a newer trusted revision is still
        // pending. Remove leftovers after a manual recovery or successful
        // update so they cannot accumulate on the SD card indefinitely.
        std::remove(config::AppUpdateStagingPath);
        return remoteAvailable?AppUpdateOutcome::Current:AppUpdateOutcome::CheckUnavailable;
    }
    if(latest->fileBytes>config::MaxAppUpdateBytes){
        error="HQ "+latest->version+" exceeds the application update size limit";
        return AppUpdateOutcome::Failed;
    }
    struct stat installed{};
    if(stat(config::AppWuhbPath,&installed)!=0||!S_ISREG(installed.st_mode)){
        error="The canonical HQ WUHB was not found at sd:/wiiu/apps/Unbound-HQ/Unbound-HQ.wuhb. Reinstall HQ there before using automatic updates.";
        return AppUpdateOutcome::Failed;
    }
    const std::string prompt="Installed HQ: "+std::string(config::AppVersion)+
        " (revision "+std::to_string(config::AppReleaseRevision)+")\nNewest HQ: "+latest->version+
        " (revision "+std::to_string(latest->revision)+")\nDownload: "+
        packageBytes(latest->fileBytes)+
        "\n\nA downloads the required HQ update. B continues in read-only mode: no game files can be changed during this run.";
    if(!confirmAction(ui,"Required HQ update",prompt,"UPDATE HQ"))
        return AppUpdateOutcome::Declined;
    DownloadControl control;
    DownloadInputContext inputContext{ui,control};
    control.setInputPoll(pollDownloadInput,&inputContext);
    ProgressCallback progress=[&ui,&control](const std::string&,const std::string& detail,int percent){
        downloadScreen(ui,control,"Updating Unbound HQ",detail,percent);
    };
    bool stagedReady=false;
    struct stat staged{};
    if(stat(config::AppUpdateStagingPath,&staged)==0&&S_ISREG(staged.st_mode)&&
       static_cast<unsigned long long>(staged.st_size)==latest->fileBytes){
        std::string stagedError;
        stagedReady=verifyDownloadedPackage(config::AppUpdateStagingPath,
                                             latest->sha256,stagedError);
    }
    if(stagedReady){
        ui.loading("Updating Unbound HQ","Using the previously verified HQ download...",100);
    }else{
        ui.downloadLoading("Updating Unbound HQ","Starting the verified application download...",0);
        const auto maximum=std::min<unsigned long long>(latest->fileBytes,
                                                         config::MaxAppUpdateBytes);
        if(!downloadFile(latest->downloadUrl,config::AppUpdateStagingPath,error,progress,
                         maximum,&latest->sha256,&control,latest->fileBytes)){
            downloadWasCancelled=control.cancelled();
            return downloadWasCancelled?AppUpdateOutcome::Declined:AppUpdateOutcome::Failed;
        }
    }
    struct stat downloaded{};
    if(stat(config::AppUpdateStagingPath,&downloaded)!=0||!S_ISREG(downloaded.st_mode)||
       static_cast<unsigned long long>(downloaded.st_size)!=latest->fileBytes){
        std::remove(config::AppUpdateStagingPath);
        error="The verified HQ download did not match its declared file size";
        return AppUpdateOutcome::Failed;
    }
    ui.installLoading("Installing Unbound HQ","Preserving the current WUHB and committing the update...",100);
    if(!installApplicationUpdate(config::AppUpdateStagingPath,error)){
        return AppUpdateOutcome::Failed;
    }
    return AppUpdateOutcome::Installed;
}
bool useInternetPackageSource=false;
bool installCoreStep(Ui& ui,GameInstall& install,const Release& release,std::size_t step,std::size_t count,std::string& error,bool replayFull=false,bool automaticLocalSource=false){
    if(!install.destinationReady){error=install.destinationWarning.empty()?"The selected installation is unavailable":install.destinationWarning;return false;}
    if(!(replayFull&&release.kind==ReleaseKind::Full)&&!releaseAppliesToInstalledVersion(release,install.modVersion)){
        if(release.kind==ReleaseKind::Patch)error="Patch "+release.version+" supports "+release.fromVersion+" or newer prior patch levels, but "+install.modVersion+" is installed";
        else error="Full package "+release.version+" is not newer than installed version "+install.modVersion;
        return false;
    }
    const std::string cachedArchive=coreRecoveryArchivePath(release);
    std::string archive=cachedArchive;
    const std::string kind=release.kind==ReleaseKind::Patch?"patch":"base";
    const std::string label="Unbound "+release.version+" "+kind+" ("+std::to_string(step+1)+"/"+std::to_string(count)+")";
    for(const auto& candidate:useInternetPackageSource?std::vector<std::string>{}:sdPackageCandidates(release.downloadUrl,selectedSdPackageRoot())){
        struct stat local{};
        if(lstat(candidate.c_str(),&local)!=0||!S_ISREG(local.st_mode))continue;
        if(!automaticLocalSource&&!confirmAction(ui,"Update found on SD card",
            label+"\n\nsd:"+candidate.substr(sizeof("fs:/vol/external01")-1)+
            "\n\nInstall this ZIP directly from SD? HQ will verify it against the release before changing game files. Your ZIP will be kept.\n\nB uses the saved download or internet instead.","USE SD PACKAGE"))break;
        DownloadControl checkingControl;
        DownloadInputContext checkingInput{ui,checkingControl};
        checkingControl.setInputPoll(pollDownloadInput,&checkingInput);
        const ProgressCallback checking=[&](const std::string&,const std::string& detail,int percent){
            ui.loading("Verifying SD update",detail,percent,true,"Cancel verification");
        };
        const auto checked=checkDownloadedPackage(candidate,release.sha256,release.compressedBytes,error,checking,&checkingControl);
        if(checked==PackageVerificationStatus::Valid){archive=candidate;break;}
        if(checked==PackageVerificationStatus::Cancelled){downloadWasCancelled=true;return false;}
        if(checked==PackageVerificationStatus::MetadataMismatch)return false;
        if(!automaticLocalSource&&!confirmAction(ui,"SD package could not be used",
            "The ZIP is incomplete, unreadable, or does not match this release. Your file was left unchanged.\n\nUse the saved download or download a verified copy from the internet?","USE INTERNET",true)){
            downloadWasCancelled=true;return false;
        }
        error.clear();break;
    }
    if(archive==cachedArchive&&!getPackage(ui,label,release.downloadUrl,release.sha256,archive,error,release.compressedBytes))return false;
    bool armed=false;
    const auto beforeMutation=[&](std::string& failure){
        if(armed)return true;
        auto writing=recoveryRecord;
        if(!markCoreRecoveryInstalling(writing,failure)||!saveCoreRecovery(writing,failure)){
            failure="Game files were not changed. Could not save the install repair record: "+failure;return false;
        }
        recoveryRecord=std::move(writing);armed=true;return true;
    };
    ui.loading("Checking "+label,"Opening the package and checking its destination before unpacking. Keep the console on.",-1);
    ProgressCallback progress=[&ui,&label](const std::string& stage,const std::string& detail,int percent){ui.installLoading(stage+": "+label,detail,percent);};
    if(!installCoreArchive(archive,install,release.version,error,progress,beforeMutation)){
        if(!armed&&!coreRecoveryNeedsRepair(recoveryRecord))
            error+="\n\nInstallation has not started. Your saved download was kept; correct the issue and retry. The installed version was not changed.";
        else error+="\n\nRetry this same package before launching the game. The installed version marker was not advanced.";
        return false;
    }
    install.modVersion=release.version;
    auto committed=recoveryRecord;
    if(!advanceCoreRecovery(committed,release.version,error)||!saveCoreRecovery(committed,error)){
        error="The package finished, but HQ could not save its completed state. Keep this SD card and retry in Install / Update. The downloaded ZIP was retained.\n\n"+error;
        return false;
    }
    recoveryRecord=std::move(committed);
    if(archive==cachedArchive)std::remove(archive.c_str());
    return true;
}
bool installCorePlan(Ui& ui,GameInstall& install,const std::vector<const Release*>& plan,std::string& error,bool replayFull=false,bool automaticLocalSource=false){
    downloadWasCancelled=false;
    refreshRecovery(true);
    if(recoveryStatus==CoreRecoveryLoadStatus::Invalid){error=recoveryWarning;return false;}
    if(recoveryStatus==CoreRecoveryLoadStatus::Missing){
        CoreRecoveryRecord started;
        if(!beginCoreRecovery(install,plan,replayFull,started,error)||!saveCoreRecovery(started,error))return false;
        recoveryRecord=std::move(started);recoveryStatus=CoreRecoveryLoadStatus::Pending;
    }else if(!coreRecoveryMatchesInstall(recoveryRecord,install)){
        error="Select the unfinished install's exact storage and language before resuming. No game files were changed.";return false;
    }
    auto reconciled=recoveryRecord;
    const auto state=reconcileCoreRecovery(reconciled,install.modVersion,error);
    if(state==CoreRecoveryReconcile::Conflict)return false;
    if(state==CoreRecoveryReconcile::Advanced||state==CoreRecoveryReconcile::Completed){
        if(!saveCoreRecovery(reconciled,error))return false;
        recoveryRecord=std::move(reconciled);
    }
    while(recoveryRecord.currentStep<recoveryRecord.plan.size()){
        const auto step=recoveryRecord.currentStep;
        const auto release=recoveryRecord.plan[step];
        if(!installCoreStep(ui,install,release,step,recoveryRecord.plan.size(),error,recoveryRecord.replayFull&&step==0,automaticLocalSource)){
            if(downloadWasCancelled)error="Operation stopped safely. Reopen Install / Update to retry package "+std::to_string(step+1)+" of "+std::to_string(recoveryRecord.plan.size())+".\n\n"+error;
            else error="Package "+std::to_string(step+1)+" of "+std::to_string(recoveryRecord.plan.size())+" stopped.\n"+error;
            return false;
        }
    }
    if(!clearCoreRecovery(recoveryRecord,error)){
        error="All game files are installed, but the repair reminder could not be cleared. Retry Install / Update to finish without redownloading.\n\n"+error;return false;
    }
    refreshRecovery();
    return true;
}

#include "map_pack_flow.hpp"
#include "custom_map_flow.hpp"

enum class InstallMenuResult { Back, Exit, Completed };

InstallMenuResult runInstallMenu(Ui& ui,GameInstall& install,GameInstall& automatic,
                                InstallPreferences& saved,const std::vector<Release>& releases,
                                bool contentAllowed,bool& settingsValid,int* requestedSection=nullptr){
    if(localPackageRepairPending){
        waitModal(ui,"Finish the custom package","Open Workshop > Custom Packages and retry the original ZIP before installing other content.",true);
        if(requestedSection)*requestedSection=2;
        return InstallMenuResult::Back;
    }
    TouchBoundary boundary(ui);
    if(requestedSection)*requestedSection=-1;
    std::vector<InstallOption> options;
    InstallPreferences draft=saved;
    std::string notice,scanWarning;
    int selectedRow=install.destinationReady?3:0;
    bool redraw=true,replayCompleted=false;
    bool reviewPage=true,reviewSd=false;
    useInternetPackageSource=false;
    NavigationRepeat navigation;
    refreshRecovery(contentAllowed);
    const ProgressCallback progress=[&ui](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    const auto rescan=[&](){
        GameInstall detected;
        std::string detectionWarning,optionsWarning;
        runBackgroundOperation("Install destination scan",progress,[&](const ProgressCallback& relay){
            detectInstallation(detected,detectionWarning,relay,false);
            return scanInstallOptions(options,optionsWarning,relay);
        });
        automatic=detected;
        scanWarning=optionsWarning;
        if(!detectionWarning.empty()&&options.empty())appendWarning(scanWarning,detectionWarning);
        notice=options.empty()?"No validated game update locations found. Check storage and rescan.":
            "Locations and language folders refreshed. Settings have not changed.";
        redraw=true;
    };
    const auto preview=[&](){
        auto result=previewInstallSelection(automatic,options,draft);
        draft.languageInstallPending=(saved.languageInstallPending&&!replayCompleted)||
                                      installLanguageNeedsReplay(install,result,options);
        return result;
    };
    const auto changeChoice=[&](int direction){
        if(selectedRow>1)return;
        replayCompleted=false;
        if(selectedRow==0){
            int current=0;
            for(std::size_t index=0;index<options.size();++index){
                const auto& choice=options[index].install;
                if(choice.sku&&draft.updateDevice==choice.update.device&&
                   draft.updateTitleId==choice.sku->updateTitleId)current=static_cast<int>(index)+1;
            }
            const int count=static_cast<int>(options.size())+1;
            const int next=(current+direction+count)%count;
            if(next==0){draft.updateDevice=Device::Missing;draft.updateTitleId=0;}
            else{const auto& choice=options[static_cast<std::size_t>(next-1)].install;
                 draft.updateDevice=choice.update.device;draft.updateTitleId=choice.sku->updateTitleId;}
            // A saved language from another SKU may not exist in this target.
            // Start with Auto, then let the user choose from that folder's list.
            draft.languageFolder.clear();
        }else if(selectedRow==1){
            const auto* choice=selectedInstallOption(automatic,options,draft);
            if(!choice||choice->languages.empty())return;
            int current=0;
            for(std::size_t index=0;index<choice->languages.size();++index)
                if(foldedInstallLanguage(draft.languageFolder)==foldedInstallLanguage(choice->languages[index]))current=static_cast<int>(index)+1;
            const int count=static_cast<int>(choice->languages.size())+1;
            const int next=(current+direction+count)%count;
            draft.languageFolder=next==0?std::string{}:choice->languages[static_cast<std::size_t>(next-1)];
        }
        notice="Selection changed. Save it before installing or leaving this screen.";
        redraw=true;
    };
    const auto saveSelection=[&](bool resetAuto){
        if(!contentAllowed){waitModal(ui,"HQ update required","Restart HQ and finish its application update before changing install settings.",true);return false;}
        InstallPreferences requested=resetAuto?InstallPreferences{}:draft;
        // Preserve an unfinished language replay even if a user resets modes.
        requested.languageInstallPending=(saved.languageInstallPending&&!replayCompleted)||
                                         (!resetAuto&&draft.languageInstallPending);
        GameInstall chosen=automatic;
        std::string validationError,saveError;
        const bool validated=runBackgroundOperation("Validate install destination",progress,[&](const ProgressCallback& relay){
            return applyInstallPreferences(chosen,requested,validationError,relay,false);
        });
        if(!validated&&!resetAuto){waitModal(ui,"Selection unavailable",validationError,true);redraw=true;return false;}
        if(validated&&installLanguageNeedsReplay(install,chosen,options))requested.languageInstallPending=true;
        if(!saveInstallPreferences(requested,saveError)){
            waitModal(ui,"Settings not saved",saveError+"\n\nThe previous saved destination is unchanged.",true);
            redraw=true;return false;
        }
        saved=requested;draft=saved;install=chosen;settingsValid=true;replayCompleted=false;
        if(!validated){install.destinationReady=false;install.destinationWarning=validationError;}
        refreshInheritedRepair(ui,install);
        notice=resetAuto?"Automatic storage and language detection restored.":"Selection saved for future launches.";
        if(saved.languageInstallPending)notice+=" Complete the full install for the selected language.";
        if(!validated)waitModal(ui,"Auto restored",validationError+"\n\nAuto settings were saved. Installation stays unavailable until a valid update is found.",true);
        redraw=true;return true;
    };
    rescan();
    if(!scanWarning.empty()){
        if(options.empty())waitModal(ui,"Install locations unavailable",scanWarning,true);
        else notice="Available locations are selectable. Disconnected or unreadable locations were skipped.";
    }
    while(WHBProcIsRunning()){
        if(redraw){
            ui.setNavigation(1,false);
            const auto proposed=preview();
            const auto normalPlan=selectedInstallPlan(releases,proposed,draft.languageInstallPending);
            const auto fullPlan=selectedInstallPlan(releases,proposed,true);
            const auto recoveryNotice=unfinishedSummary();
            const auto displayNotice=notice.empty()?recoveryNotice:notice+(recoveryNotice.empty()?"":"\n"+recoveryNotice);
            reviewSd=false;
            const Release* next=normalPlan.empty()?(fullPlan.empty()?nullptr:fullPlan.front()):normalPlan.front();
            if(recoveryStatus==CoreRecoveryLoadStatus::Pending&&recoveryRecord.currentStep<recoveryRecord.plan.size())next=&recoveryRecord.plan[recoveryRecord.currentStep];
            if(next)for(const auto& path:sdPackageCandidates(next->downloadUrl)){struct stat st{};if(lstat(path.c_str(),&st)==0&&S_ISREG(st.st_mode)){reviewSd=true;break;}}
            if(reviewPage)ui.installReview(proposed,latestReleaseVersion(releases,proposed.sku),
                proposed.destinationReady?recoveryNotice:proposed.destinationWarning,selectedRow,
                contentAllowed&&proposed.destinationReady&&proposed.update.present&&(next||inheritedRepairGate.blocked()),
                reviewSd,inheritedRepairGate.blocked()||(recoveryStatus==CoreRecoveryLoadStatus::Pending),!normalPlan.empty(),
                contentAllowed&&proposed.destinationReady&&proposed.update.present&&!proposed.modVersion.empty());
            else ui.installMenu(proposed,options,draft,selectedRow,latestReleaseVersion(releases,proposed.sku),displayNotice,
                           contentAllowed,!normalPlan.empty()||recoveryStatus==CoreRecoveryLoadStatus::Pending,!fullPlan.empty(),inheritedRepairGate.blocked());
            redraw=false;
        }
        std::optional<TouchAction> touch;
        const auto input=readUiInput(ui,&touch);
        auto buttons=(input.buttons&~(button::Up|button::Down|button::Left|button::Right))|
                           navigation.update(input.buttons,input.held,SDL_GetTicks());
        int nextSection=-1;
        bool mapsOnlyRequested=false;
        if(touch){
            if(touch->kind==TouchActionKind::Sidebar&&touch->value>=0&&touch->value<=5){
                if(touch->value==1)continue;
                nextSection=touch->value;buttons|=button::B;
            }else if(touch->kind==TouchActionKind::InstallRow&&touch->value>=0&&touch->value<=5){
                selectedRow=touch->value;redraw=true;
                if(selectedRow>=2)buttons|=button::A;
            }else if(touch->kind==TouchActionKind::InstallChoice&&touch->value>=0&&touch->value<=3){
                selectedRow=touch->value/2;
                changeChoice(touch->value%2?1:-1);
                continue;
            }
        }
        if(reviewPage){
            if(touch&&touch->kind==TouchActionKind::InstallRow)buttons|=button::A;
            if(buttons&(button::Up|button::Down|button::Left|button::Right)){
                std::vector<int> rows{3,5,0};if(reviewSd)rows.push_back(4);
                const auto current=std::find(rows.begin(),rows.end(),selectedRow);
                const int index=current==rows.end()?0:int(current-rows.begin());
                const int direction=buttons&(button::Up|button::Left)?-1:1;
                selectedRow=rows[(index+direction+int(rows.size()))%int(rows.size())];redraw=true;
                buttons&=~(button::Up|button::Down|button::Left|button::Right);
            }
            if((buttons&button::X)||((buttons&button::A)&&selectedRow==0)){
                reviewPage=false;selectedRow=0;redraw=true;continue;
            }
            if(buttons&button::Y){selectedRow=5;buttons|=button::A;}
            if(buttons&button::A){
                mapsOnlyRequested=selectedRow==5;
                useInternetPackageSource=selectedRow==4;selectedRow=3;
            }
        }else if((buttons&button::B)&&nextSection<0){
            if(!sameInstallPreferences(draft,saved)&&!confirmAction(ui,"Discard unsaved selection?","Keep the saved storage and language?","DISCARD")){redraw=true;continue;}
            draft=saved;reviewPage=true;selectedRow=3;redraw=true;continue;
        }
        if(buttons&button::Plus)return InstallMenuResult::Exit;
        if(buttons&button::B){
            if(sameInstallPreferences(draft,saved)||confirmAction(ui,"Discard unsaved selection?",
                "The saved destination and language will stay unchanged.","DISCARD")){
                if(requestedSection)*requestedSection=nextSection;
                return InstallMenuResult::Back;
            }
            redraw=true;
            continue;
        }
        if(!reviewPage&&(buttons&button::Y)){mapsOnlyRequested=true;selectedRow=3;buttons|=button::A;}
        if(buttons&button::X)rescan();
        if(buttons&button::Up){selectedRow=(selectedRow+4)%5;redraw=true;}
        if(buttons&button::Down){selectedRow=(selectedRow+1)%5;redraw=true;}
        if(buttons&button::Left)changeChoice(-1);
        if(buttons&button::Right)changeChoice(1);
        if(buttons&button::A){
            if(selectedRow<2)changeChoice(1);
            else if(selectedRow==2){if(saveSelection(false)){reviewPage=true;selectedRow=3;}redraw=true;}
            else if(selectedRow==4){
                if(confirmAction(ui,"Reset install settings?","Use automatic update storage and language detection again? No game files will be moved or removed.","RESET AUTO"))saveSelection(true);
                redraw=true;
            }else{
                redraw=true;
                if(!contentAllowed){waitModal(ui,"HQ update required","Restart HQ and finish its application update before installing game content.",true);continue;}
                if(!settingsValid||!sameInstallPreferences(draft,saved)){
                    if(!confirmAction(ui,"Save this install selection?","Remember this destination and language before downloading? No game files are changed by saving.","SAVE SELECTION")||!saveSelection(false))continue;
                }
                GameInstall validated=automatic;
                std::string validationError;
                if(!runBackgroundOperation("Validate install destination",progress,[&](const ProgressCallback& relay){
                    return applyInstallPreferences(validated,saved,validationError,relay,false);
                })){
                    install=validated;
                    waitModal(ui,"Install destination unavailable",validationError,true);continue;
                }
                install=validated;
                refreshRecovery(true);
                const auto inherited=ensureInheritedRepair(ui,install,contentAllowed);
                if(inherited==InheritedRepairOutcome::Blocked){notice=unfinishedSummary();continue;}
                if(recoveryStatus==CoreRecoveryLoadStatus::Invalid){
                    waitModal(ui,"Install record needs attention",recoveryWarning+
                        "\n\nHQ will not overwrite an unreadable repair record. Export Support & Stats and preserve "+coreRecoveryStatePath()+" before manual recovery.",true);continue;
                }
                if(recoveryStatus==CoreRecoveryLoadStatus::Pending&&!coreRecoveryMatchesInstall(recoveryRecord,install)){
                    if(canDiscardCoreRecoveryDownload(recoveryRecord)&&confirmAction(ui,"Leave the paused download?",
                        unfinishedSummary()+"\n\nNo game files were written by this saved installation. Forget its reminder and use the newly selected destination? Its downloaded data stays in the cache.","USE SELECTION")){
                        std::string discardError;
                        if(!discardCoreRecoveryDownload(recoveryRecord,discardError)){waitModal(ui,"Paused download retained",discardError,true);continue;}
                        refreshRecovery();
                    }else{
                        const auto required=coreRecoveryPreferences(recoveryRecord);
                        const auto available=previewInstallSelection(automatic,options,required);
                        if(available.destinationReady&&coreRecoveryMatchesInstall(recoveryRecord,available)){
                            if(confirmAction(ui,"Resume the saved installation?",unfinishedSummary()+
                                "\n\nSelect its official UPDATE location to finish safely. This does not move the game or install files to the disc."
                                "\n\nRequired update:\n"+coreRecoveryTargetRoot(recoveryRecord),"SELECT SAVED UPDATE")){
                                draft=required;
                                if(saveSelection(false))notice="Saved UPDATE destination selected. Press A to resume its installation.";
                            }
                        }else waitModal(ui,"Saved update location unavailable",
                            "HQ cannot find the official UPDATE location used by the unfinished installation. "
                            "Reconnect that storage and rescan. HQ has kept the saved download and repair record."
                            "\n\nRequired update:\n"+coreRecoveryTargetRoot(recoveryRecord)+
                            "\n\nIf the update was moved to another drive, export a Support report before resetting its repair record.",true);
                        continue;
                    }
                }
                if(mapsOnlyRequested&&(saved.languageInstallPending||recoveryStatus==CoreRecoveryLoadStatus::Pending||
                   install.modVersion.empty()||(!sdCoreVersion.empty()&&compareVersions(install.modVersion,sdCoreVersion)<0))){
                    waitModal(ui,"Finish Core first","Install or resume the compatible Core release before reinstalling map packs. No map files were changed.",true);continue;
                }
                bool fullReplay=!mapsOnlyRequested&&(saved.languageInstallPending||!reviewPage);
                if(recoveryStatus==CoreRecoveryLoadStatus::Pending){
                    auto refreshed=recoveryRecord;
                    bool sizeChanged=false;
                    std::string refreshError;
                    if(!refreshCoreRecoveryDownloadSize(refreshed,releases,sizeChanged,refreshError)){
                        waitModal(ui,"Saved download needs attention",refreshError,true);continue;
                    }
                    if(sizeChanged){
                        if(!saveCoreRecovery(refreshed,refreshError)){
                            waitModal(ui,"Could not save corrected download size",
                                "The saved package was retained and no new download or game-content write began.\n\n"+refreshError,true);continue;
                        }
                        recoveryRecord=std::move(refreshed);
                    }
                }
                auto plan=mapsOnlyRequested?std::vector<const Release*>{}:selectedInstallPlan(releases,install,fullReplay);
                const bool resuming=recoveryStatus==CoreRecoveryLoadStatus::Pending;
                bool freshRestart=false;
                if(resuming){
                    auto checked=recoveryRecord;
                    std::string conflictError;
                    if(reconcileCoreRecovery(checked,install.modVersion,conflictError)==CoreRecoveryReconcile::Conflict){
                        const auto freshPlan=selectedInstallPlan(releases,install,true);
                        CoreRecoveryRecord replacement;
                        std::string restartError;
                        if(!prepareCoreRecoveryRestart(recoveryRecord,install,freshPlan,replacement,restartError)){
                            waitModal(ui,"Fresh download unavailable",conflictError+"\n\n"+restartError,true);continue;
                        }
                        const std::string body="The installed version no longer matches the unfinished installation.\n\n"
                            "Press A to download Unbound "+replacement.plan.back().version+
                            " from the beginning and reinstall the full release with its required patches.\n\nDestination: "+
                            deviceName(install.update.device)+"\nLanguage: "+install.languageFolder+
                            "\n\n"+planDescription(freshPlan)+
                            "\n\nSaved downloads for these packages will be cleared. Keep the game closed until installation finishes.";
                        if(!confirmAction(ui,"Installation stopped",body,"DOWNLOAD FRESH",true))continue;
                        ui.loading("Preparing fresh download","Clearing saved data for this release and restarting its install plan...",0);
                        if(!runBackgroundOperation("Reset saved package download",progress,[&](const ProgressCallback&){
                            return restartCoreRecovery(recoveryRecord,replacement,restartError);
                        })){
                            waitModal(ui,"Fresh download could not start",restartError+
                                "\n\nThe unfinished installation is still recorded. Fix SD access and retry.",true);
                            refreshRecovery();continue;
                        }
                        recoveryRecord=std::move(replacement);
                        freshRestart=true;
                    }
                    fullReplay=recoveryRecord.replayFull;plan.clear();
                    for(std::size_t step=recoveryRecord.currentStep;step<recoveryRecord.plan.size();++step)plan.push_back(&recoveryRecord.plan[step]);
                }
                if(plan.empty()&&!resuming&&latestReleaseVersion(releases,install.sku).empty()){
                    waitModal(ui,"No install path","The release feed has no complete compatible package path for this destination. No game files were changed.",true);continue;
                }
                std::vector<MapJob> mapJobs;
                if(!chooseMapPacks(ui,install,mapJobs,mapsOnlyRequested))continue;
                if(plan.empty()&&!resuming&&mapJobs.empty()){
                    std::string migrationError;
                    if(!finishContentMigration(ui,install,migrationError)){waitModal(ui,"Plugin needs attention",migrationError,true);continue;}
                    waitModal(ui,"Everything is up to date",mapsOnlyRequested?"Your selected map packs are current. Core was left unchanged.":"Core and your selected map packs are current. Press A to return to HQ.");
                    return InstallMenuResult::Completed;
                }
                const auto officialWarning=officialUpdateWarning(install);
                if(!officialWarning.empty()&&!confirmAction(ui,"Official game update",officialWarning,"CONTINUE",true))continue;
                if(!freshRestart&&!confirmContentQueue(ui,install,plan,mapJobs))continue;
                // Persist a replay before the first full package. Cancellation,
                // an interrupted patch, or restart must not trust the old marker.
                if(fullReplay&&!saved.languageInstallPending){
                    InstallPreferences pending=saved;pending.languageInstallPending=true;
                    std::string pendingError;
                    if(!saveInstallPreferences(pending,pendingError)){waitModal(ui,"Cannot remember install state",pendingError,true);continue;}
                    saved=pending;draft=saved;
                }
                std::string installError;
                replayCompleted=false;
                const auto operationStarted=SDL_GetTicks();
                if((plan.empty()&&!resuming)||installCorePlan(ui,install,plan,installError,fullReplay)){
                    if(saved.languageInstallPending){
                        InstallPreferences completed=saved;completed.languageInstallPending=false;
                        std::string completionError;
                        if(!saveInstallPreferences(completed,completionError)){
                            replayCompleted=true;
                            waitModal(ui,"Files installed; settings need attention",completionError+
                                "\n\nThe game files are installed, but HQ could not clear the language-install reminder. Fix SD write access and select Save selection before exiting.",true);
                            rescan();draft=completed;notice="Files installed. Save selection to clear the remaining language-install reminder.";
                            selectedRow=2;continue;
                        }
                        saved=completed;draft=saved;
                    }
                    if(!finishContentMigration(ui,install,installError)){
                        waitModal(ui,"Core installed; plugin needs attention",installError,true);continue;
                    }
                    if(!installMapJobs(ui,install,mapJobs,installError)){
                        waitModal(ui,"Map installation paused",installError+"\n\nCore is installed. Reopen Install / Update to retry the unfinished map package. Completed packs are kept.",true);continue;
                    }
                    notice="Core "+install.modVersion+" and your selected map packs are ready.\nDestination: "+
                        std::string(deviceName(install.update.device))+" / "+install.languageFolder+".";
                    const auto elapsedSeconds=(SDL_GetTicks()-operationStarted)/1000;
                    waitModal(ui,"Unbound is ready",notice+"\nCompleted in "+std::to_string(elapsedSeconds/60)+" min "+std::to_string(elapsedSeconds%60)+
                        " sec.\n\nPress A to return to HQ."+
                        (pluginRestartRequired?" Restart the console/Aroma first to use the updated plugins.":""));
                    lastSessionError.clear();
                    return InstallMenuResult::Completed;
                }else{
                    if(!downloadWasCancelled){
                        const std::string failure="HQ "+std::string(config::AppVersion)+" / r"+std::to_string(config::AppReleaseRevision)+
                            "\nDestination: "+install.update.root+"\nLanguage: "+install.languageFolder+
                            "\nInstalled marker: "+install.modVersion+"\n"+installError;
                        std::string failureSaveError;
                        if(!writeTextFileAtomic(config::gameInstallFailurePath(),
                            failure.substr(0,DiagnosticsMaximumInstallFailureBytes),failureSaveError))
                            installError+="\n\nCould not save this failure for Support: "+failureSaveError;
                    }
                    if(downloadWasCancelled)waitModal(ui,"Installation paused",
                        "Your progress was kept. Return to Install / Update when you are ready to continue.");
                    else{
                        const auto feedback=installFailureFeedback(installError);
                        waitModal(ui,feedback.title,feedback.message,true);
                        lastSessionError=installError;
                    }
                    // A failed operation does not invalidate discovery. The next
                    // attempt already revalidates its exact drive and language.
                    // Avoid a redundant multi-drive scan after every failure.
                    refreshRecovery();notice=unfinishedSummary();
                }
            }
        }
        waitFrame(ui);
    }
    return InstallMenuResult::Back;
}

#include "quick_update_flow.hpp"
#include "local_package_flow.hpp"

#ifdef UNBOUND_DIAGNOSTIC_SD
// A separate, local-only entry point. Do not run the public self-updater,
// content release planner, startup plugin updates or network news in this build.
std::uint64_t runSdDiagnostic(Ui& ui,const ProgressCallback& progress){
    DiagnosticArchiveInfo archive;
    std::string error;
    if(!inspectDiagnosticArchive(archive,error,progress)){
        waitModal(ui,"Test ZIP unavailable",error,true);return 0;
    }
    if(!archive.present){
        waitModal(ui,"No SD test ZIP",
            "Place a normal rollout ZIP at sd:/unbound/testing/update.zip, then reopen HQ SD Test. "
            "No companion manifest or GitHub release is needed. Nothing was installed.");
        return 0;
    }

    GameInstall install;
    if(!detectInstallation(install,error,progress,false)){
        waitModal(ui,"Test destination unavailable",error,true);return 0;
    }
    // Read, never modify, the regular HQ's saved destination/language. Keeping
    // the diagnostic app's own state separate must not ignore a USB override.
    const std::string canonicalSettings=std::string(config::PublishedSdRoot)+"/settings.ini";
    const std::string legacySettings=std::string(config::LegacyHqSdRoot)+"/settings.ini";
    struct stat canonicalInfo{};
    const char* regularSettings=stat(canonicalSettings.c_str(),&canonicalInfo)==0
        ?canonicalSettings.c_str():legacySettings.c_str();
    InstallPreferences preferences;
    struct stat settingsInfo{};
    for(const auto& suffix:{std::string(".unbound-new"),std::string(".unbound-old")}){
        const auto path=std::string(regularSettings)+suffix;
        if(stat(path.c_str(),&settingsInfo)==0||errno!=ENOENT){
            waitModal(ui,"HQ settings need recovery",
                "Open regular HQ to finish its settings recovery before testing. No test files were installed.",true);
            return 0;
        }
    }
    if(stat(regularSettings,&settingsInfo)==0){
        std::string settings;
        if(!S_ISREG(settingsInfo.st_mode)||!readTextFile(regularSettings,settings,config::MaxSettingsBytes)||
           !parseInstallPreferences(settings,preferences,error)){
            waitModal(ui,"Saved destination unreadable",
                "Check Install / Update settings in regular HQ before testing. No automatic fallback was used.\n\n"+error,true);
            return 0;
        }
    }else if(errno!=ENOENT){
        waitModal(ui,"Saved destination unreadable",
            "Could not inspect regular HQ's settings.ini. No test files were installed.",true);return 0;
    }
    if(!runBackgroundOperation("Check local test destination",progress,[&](const ProgressCallback& relay){
        return applyInstallPreferences(install,preferences,error,relay,false);
    })||!install.sku||!install.update.present||!install.destinationReady){
        waitModal(ui,"Test destination unavailable",error.empty()?install.destinationWarning:error,true);return 0;
    }
    std::string summary="LOCAL TEST ONLY: sd:/unbound/testing/update.zip\nSize: "+packageBytes(archive.compressedBytes)+
        "\nGame: "+install.sku->label+"\nDestination: "+deviceName(install.update.device)+
        "\nLanguage: "+install.languageFolder+"\n\n"+install.update.root+
        "\n\nThis replaces real installed game files. No bulk backups are made. "
        "A patch ZIP needs its base content installed already. To change storage/language, cancel and use regular HQ's Install / Update settings."
        "\n\nThe version marker becomes 0.0.0 before test files are written. Regular HQ will offer a full public reinstall afterward. "
        "No GitHub, app or plugin update will run here. The ZIP stays on SD and is offered again each launch.";
    const auto officialWarning=officialUpdateWarning(install);
    if(!officialWarning.empty())summary+="\n\n"+officialWarning;
    if(!confirmAction(ui,"Install SD test update?",summary,"INSTALL TEST ZIP"))return 0;

    const auto began=SDL_GetTicks();
    const ProgressCallback installProgress=[&ui](const std::string& title,const std::string& detail,int percent){
        ui.installLoading(title,detail,percent);
    };
    const bool installed=installDiagnosticArchive(archive,install,error,installProgress);
    const auto elapsed=(SDL_GetTicks()-began)/1000;
    std::string report="UNBOUND HQ SD TEST\nApp: "+std::string(config::AppVersion)+
        "\nZIP: sd:/unbound/testing/update.zip\nCompressed bytes: "+std::to_string(archive.compressedBytes)+
        "\nZIP SHA256: "+archive.sha256+"\nDestination: "+install.update.root+
        "\nLanguage: "+install.languageFolder+"\nMarker used: 0.0.0\nResult: "+(installed?"OK":"FAILED")+
        "\nElapsed seconds: "+std::to_string(elapsed)+"\n";
    if(!error.empty())report+="Error: "+error+"\n";
    std::string reportError;
    const bool reportSaved=writeTextFileAtomic(std::string(config::SdRoot)+"/sd-test-report.txt",report,reportError);
    if(!installed){
        waitModal(ui,"SD test install stopped",error+
            "\n\nDo not launch the game after a partial install. Retry this same ZIP, "
            "or use regular HQ to reinstall the full public release. No bulk backups exist.\n\n"+
            (reportSaved?"A test report was saved in the HQ SD Test app folder.":"Report could not be saved: "+reportError),true);
        return 0;
    }
    summary="Local ZIP installed for "+install.languageFolder+" on "+deviceName(install.update.device)+
        ".\nVersion marker: 0.0.0 (diagnostic only).\nElapsed: "+std::to_string(elapsed/60)+"m "+
        std::to_string(elapsed%60)+"s.\n\nThe ZIP was kept for repeat testing. "
        "SD redirection overrides can mask files you just installed; disable conflicting overrides when testing.";
    summary+=reportSaved?"\n\nReport: sd:/wiiu/apps/BO2-Unbound-HQ-SDTest/sd-test-report.txt":
        "\n\nReport could not be saved: "+reportError;
    if(!install.base.present){
        waitModal(ui,"SD test installed",summary+"\n\nNo installed base game or supported inserted disc was detected. Return to the Wii U Menu to continue.");
        return 0;
    }
    return confirmAction(ui,"SD test installed",summary+
        "\n\nA launches Black Ops II. B returns to the Wii U Menu.","LAUNCH GAME")?install.sku->baseTitleId:0;
}
#endif
}

int main(){
    WHBProcInit();initializeInput();WHBLogCafeInit();Ui ui;std::string error;
    if(!ui.initialize(error)){WHBLogPrintf("UI startup failed: %s",error.c_str());WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return 1;}
    ui.bootScreen();for(int i=0;i<75&&WHBProcIsRunning();++i)waitFrame(ui);
    if constexpr (!config::DiagnosticSdBuild) {
        const auto migration=migrateApplicationPath(error);
        if(migration!=AppPathMigration::Unchanged){
            const bool failed=migration==AppPathMigration::Failed;
            waitModal(ui,failed?"HQ rename needs attention":"Unbound HQ moved",
                failed?error:"HQ and its settings now live in sd:/wiiu/apps/Unbound-HQ. Restart your Wii U, then open Unbound HQ again.",failed);
            SYSLaunchMenu();
            while(WHBProcIsRunning())waitFrame(ui);
            ui.shutdown();WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return failed?1:0;
        }
    }
    ProgressCallback startup=[&ui](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
    if(!initializePlatform(error,startup)){waitModal(ui,"Startup error",error,true);shutdownPlatform();ui.shutdown();WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return 1;}
    std::string supplyRunPreference,supplyRunPreferenceError;
    if(readInstallState(config::SupplyRunSettingsPath,supplyRunPreference,supplyRunPreferenceError))
        ui.setSupplyRunEnabled(trim(supplyRunPreference)!="off");
    else lastSessionError="Supply Run preference could not be read: "+supplyRunPreferenceError;
    setOperationFrameCallback([&ui]{
        if(ui.operationScreenActive()){
            readUiInput(ui);
            ui.animateDownloadWaiting();
        }
    });

#ifdef UNBOUND_DIAGNOSTIC_SD
    const auto diagnosticLaunch=runSdDiagnostic(ui,startup);
    ui.loading(diagnosticLaunch?"Launching Black Ops II":"Returning to the Wii U Menu",
               "Closing the local test installer and storage safely...",100);
    shutdownPlatform();
    if(diagnosticLaunch)SYSLaunchTitle(diagnosticLaunch);else SYSLaunchMenu();
    while(WHBProcIsRunning())waitFrame(ui);
    ui.shutdown();WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return 0;
#endif

    std::string appUpdateWarning,sharedReleaseFeed;
    const auto appUpdate=checkForApplicationUpdate(ui,sharedReleaseFeed,
                                                    appUpdateWarning,error);
    const bool contentUpdatesAllowed=appUpdate==AppUpdateOutcome::Current||
                                     appUpdate==AppUpdateOutcome::CheckUnavailable;
    if(appUpdate==AppUpdateOutcome::Installed){
        waitModal(ui,"Unbound HQ updated","The new HQ application was installed successfully. Press A to return to the Wii U Menu, then relaunch Unbound HQ.");
        ui.loading("Returning to the Wii U Menu","Closing storage safely. Relaunch HQ to use the new version...",100);
        shutdownPlatform();SYSLaunchMenu();while(WHBProcIsRunning())waitFrame(ui);
        ui.shutdown();WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return 0;
    }
    if(appUpdate==AppUpdateOutcome::Declined)
        waitModal(ui,"HQ update postponed","The current HQ app was not changed. Game-content updates are disabled until HQ is restarted and updated.",true);
    else if(appUpdate==AppUpdateOutcome::Failed)
        waitModal(ui,"HQ update failed",error+"\n\nNo game-content update will run in this session. Follow any recovery instruction above before relaunching HQ.",true);

    std::string startupNotices;
    bool startupNeedsAttention=false;
    const auto addStartupNotice=[&](const std::string& section,const std::string& body){
        if(!body.empty())appendWarning(startupNotices,section+"\n"+body);
    };

    GameInstall install,automaticInstall;
    InstallPreferences installPreferences;
    bool installSettingsLoaded=false;
    std::vector<Release> releases;
    std::string changes,updateStatus,networkWarning;
    unsigned availableMapUpdates=0;
    const auto loadGame=[&](){
        mapRepairPending=false;
        inheritedRepairGate.clear();
        sessionMapCatalogLoaded=false;
        sessionMapCatalog.clear();
        sessionMapNotice.clear();
        sessionCustomMaps={};sessionMods={};
        bool releasesChanged=false,changesChanged=false,downloadedArtwork=false;
        install={};
        install.game=config::activeGame;
        if(!detectInstallation(install,error,startup,contentUpdatesAllowed))
            addStartupNotice("STORAGE",error);
        automaticInstall=install;
        installPreferences={};
        std::string installSettingsWarning;
        installSettingsLoaded=loadInstallPreferences(installPreferences,installSettingsWarning,contentUpdatesAllowed);
        if(installSettingsLoaded){
            std::string selectionError;
            if(!runBackgroundOperation("Apply saved install selection",startup,[&](const ProgressCallback& relay){
                return applyInstallPreferences(install,installPreferences,selectionError,relay,contentUpdatesAllowed);
            }))appendWarning(installSettingsWarning,selectionError);
        }else{
            install.destinationReady=false;
            install.destinationWarning=installSettingsWarning;
        }
        addStartupNotice("INSTALL DESTINATION",installSettingsWarning);
        refreshRecovery(contentUpdatesAllowed);
        refreshLocalPackageRecovery();
        if(localPackageRepairPending){
            addStartupNotice("CUSTOM PACKAGE",localPackageWarning.empty()?"An SD ZIP installation needs to finish. Open Workshop > Custom Packages to resume.":localPackageWarning);
            startupNeedsAttention=true;
        }
        if(!config::ghostsMode()&&install.sku&&install.update.present){
            refreshInheritedRepair(ui,install);
            if(inheritedRepairGate.blocked()&&contentUpdatesAllowed){
                if(ensureInheritedRepair(ui,install,contentUpdatesAllowed)==InheritedRepairOutcome::Repaired)
                    waitModal(ui,"Original game files restored","The missing original files were restored without downloading the game package again. Your current mod files were kept.");
            }
            if(inheritedRepairGate.blocked()){
                addStartupNotice("ORIGINAL GAME FILES",unfinishedSummary());startupNeedsAttention=true;
            }
        }
        if(recoveryStatus!=CoreRecoveryLoadStatus::Missing){
            addStartupNotice("UNFINISHED INSTALLATION",unfinishedSummary()+
                (recoveryWarning.empty()?"":"\n"+recoveryWarning));
            startupNeedsAttention=startupNeedsAttention||repairNeeded();
        }
        if(!installSettingsWarning.empty())startupNeedsAttention=true;
        if(install.sku){
            const auto officialWarning=officialUpdateWarning(install);
            if(!officialWarning.empty()){
                addStartupNotice("OFFICIAL GAME UPDATE",officialWarning);
                startupNeedsAttention=true;
            }
        }
        std::vector<MapJob> pendingMaps;
        std::string mapPendingError;
        if(!readMapPending(install,pendingMaps,mapPendingError)||!pendingMaps.empty()){
            mapRepairPending=true;
            addStartupNotice("MAP INSTALLATION","A map installation needs to finish. Open Install / Update to resume.");
        }
        fetchCatalog(ui,config::ghostsMode()?nullptr:&sharedReleaseFeed,releases,changes,
                     updateStatus,networkWarning,releasesChanged,changesChanged);
        if(!config::ghostsMode()&&contentUpdatesAllowed&&install.update.present&&install.destinationReady&&!repairNeeded()&&!sdCoreVersion.empty()&&compareVersions(install.modVersion,sdCoreVersion)>=0){
            std::string migrationError;
            if(!finishContentMigration(ui,install,migrationError)){
                addStartupNotice("OLD PLUGIN NEEDS ATTENTION",migrationError);startupNeedsAttention=true;
            }else if(retiredPluginRestartRequired){
                addStartupNotice("RESTART REQUIRED","The retired SD plugin has been moved out of Aroma. Fully restart the console before playing.");startupNeedsAttention=true;
            }
        }
        if(appUpdate!=AppUpdateOutcome::Failed)appendWarning(networkWarning,appUpdateWarning);
        const auto newsImages=cacheNewsImages(ui,changes,networkWarning,downloadedArtwork);
        const bool newsUpdated=changesChanged||downloadedArtwork;
        ui.loading(newsUpdated?"Updating What's New":"Opening Unbound HQ",newsUpdated?"Preparing the new update artwork...":"Reusing unchanged update artwork...",85);ui.setNewsImages(newsImages);
        ui.loading(newsUpdated?"Updating What's New":"Opening Unbound HQ",newsUpdated?"Building the updated What's New display...":"Preparing the unchanged What's New display...",95);ui.prepareNews(changes);
        addStartupNotice("NETWORK AND RELEASES",networkWarning);
        if(releases.empty())startupNeedsAttention=true;
        availableMapUpdates=contentUpdatesAllowed?installedMapUpdateCount(ui,install):0;
    };
    std::string retirementReminder,retirementError;
    const auto retirementPath=std::string(config::SdRoot)+"/state/retirement-restart.txt";
    if(!readInstallState(retirementPath,retirementReminder,retirementError)||!retirementReminder.empty()){
        retiredPluginRestartRequired=true;pluginRestartRequired=true;
        if(retirementError.empty()&&confirmAction(ui,"Console restart needed",
            "HQ retired the old Mod Link plugin. Have you fully restarted the console since that installation? Returning to the Wii U Menu alone does not unload Aroma plugins.","YES, I RESTARTED")){
            if(writeTextFileAtomic(retirementPath,"",retirementError)){
                retiredPluginRestartRequired=false;pluginRestartRequired=false;
            }else waitModal(ui,"Restart reminder retained",retirementError,true);
        }
    }
    loadGame();
    const auto startupPlugins=builtInPlugins();
    std::vector<std::string> postponedPlugins;
    for(const auto& plugin:startupPlugins){
        PluginRuntime startupPlugin;startupPlugin.plugin=plugin;
        const bool requiredModLink=plugin.key=="UNBOUND_MOD_LINK";
        std::string pluginSettingWarning;
        if(requiredModLink)startupPlugin.autoUpdateEnabled=true;
        else loadPluginAutoUpdateSetting(plugin.key,startupPlugin.autoUpdateEnabled,
                                         pluginSettingWarning,contentUpdatesAllowed);
        addStartupNotice(plugin.name+" AUTO-UPDATE",pluginSettingWarning);
        if(!pluginSettingWarning.empty())startupNeedsAttention=true;
        if(!shouldCheckPluginUpdateAtStartup(plugin,startupPlugin.autoUpdateEnabled))continue;
        if(!contentUpdatesAllowed){
            postponedPlugins.push_back(plugin.name);
            continue;
        }
        std::string feedError;
        const std::string* prefetched=requiredModLink&&!sharedReleaseFeed.empty()
            ? &sharedReleaseFeed : nullptr;
        startupPlugin.releaseReady=fetchTrustedPluginRelease(startupPlugin.plugin,
            startupPlugin.release,startupPlugin.feedStatus,feedError,prefetched);
        std::string inspectWarning;
        inspectPluginInstall(startupPlugin.plugin,
            startupPlugin.releaseReady?&startupPlugin.release:nullptr,
            startupPlugin.status,inspectWarning);
        if(startupPlugin.status.presence!=PluginPresence::Damaged)
            addStartupNotice(plugin.name+" INSTALL STATE",inspectWarning);
        if(!startupPlugin.releaseReady){
            addStartupNotice(plugin.name+" UPDATE UNAVAILABLE",feedError+
                "\nHQ will continue normally and the installed plugin was not changed.");
        }else if(startupPlugin.status.presence==PluginPresence::NotInstalled&&requiredModLink){
            if(downloadAndInstallPlugin(ui,startupPlugin,false,error))
                waitModal(ui,"Unbound Mod Link installed",startupPlugin.release.version+
                    " was installed successfully. Fully restart the console/Aroma environment before launching Black Ops II.");
            else waitModal(ui,"Unbound Mod Link install failed",error+
                "\n\nNo incomplete plugin installation was retained.",true);
        }else if(startupPlugin.status.presence==PluginPresence::Managed&&
                 startupPlugin.status.updateAvailable){
            if(downloadAndInstallPlugin(ui,startupPlugin,false,error))
                waitModal(ui,plugin.name+" updated",startupPlugin.release.version+
                    " was installed successfully. Fully restart the console/Aroma environment before using the updated plugin.");
            else waitModal(ui,plugin.name+" update failed",error+
                "\n\nThe prior managed installation was retained or restored.",true);
        }else if(startupPlugin.status.presence==PluginPresence::Unmanaged){
            if(requiredModLink&&confirmAction(ui,"Upgrade the existing Unbound plugin?",
                "HQ found an existing BO2-Unbound-Redirector.wps without an HQ receipt. Replace it with the verified Unbound Mod Link release and let HQ manage future plugin updates?",
                "REPLACE & MANAGE")){
                if(downloadAndInstallPlugin(ui,startupPlugin,true,error))
                    waitModal(ui,"Unbound Mod Link installed",startupPlugin.release.version+
                        " replaced the existing plugin. Fully restart the console/Aroma environment before launching Black Ops II.");
                else waitModal(ui,"Unbound Mod Link install failed",error+
                    "\n\nThe prior plugin was retained or restored.",true);
            }else if(!requiredModLink){
                addStartupNotice(plugin.name+" AUTO-UPDATE SKIPPED",
                    "A manual installation was detected. Open Workshop > "+plugin.name+
                    " and choose Replace & Manage before Auto-Update can control these files.");
            }
        }else if(startupPlugin.status.presence==PluginPresence::Damaged){
            startupNeedsAttention=true;
            addStartupNotice(plugin.name+" NEEDS ATTENTION",inspectWarning+
                "\nOpen its Workshop page and choose Repair. Auto-Update did not overwrite the damaged or interrupted installation.");
        }
    }
    if(!postponedPlugins.empty()){
        std::string names;
        for(const auto& name:postponedPlugins){if(!names.empty())names+=", ";names+=name;}
        addStartupNotice("PLUGIN UPDATES POSTPONED",names+
            " could not be updated while HQ is in read-only mode. No Aroma plugin files were changed.");
    }
    if(!startupNotices.empty())waitModal(ui,"Startup notices",startupNotices,startupNeedsAttention);
    bool testerAttempted=false;
    bool testerFailed=false;
    struct stat testerInfo{};
    if(contentUpdatesAllowed && lstat(config::TesterArchivePath,&testerInfo)==0){
        if(repairNeeded()){
            waitModal(ui,"Local test waiting","tester.zip was found. Finish the unfinished installation before installing local test content.");
        }else if(!install.update.present || !install.destinationReady){
            waitModal(ui,"Local test waiting","tester.zip was found. Choose a valid destination in Install / Update, then reopen HQ to install it.");
        }else if(confirmAction(ui,"Install local test package?",
            std::string("Found sd:/unbound/tester.zip\n\nDestination: ")+deviceName(install.update.device)+
            "\nLanguage: "+install.languageFolder+
            "\n\nInstalls this ZIP without a release-feed hash check. Only install files supplied by your tester coordinator. Test content is marked 0.0.0, not as an official release. Press B to skip.","INSTALL TEST")){
            testerAttempted=true;
            ProgressCallback testProgress=[&ui](const std::string& title,const std::string& detail,int percent){ui.installLoading(title,detail,percent);};
            std::string testError;
            testerFailed=!installTesterArchive(install,testError,testProgress);
            install.modVersion="0.0.0";
            if(testerFailed)waitModal(ui,"Test installation stopped",testError+"\n\nKeep tester.zip on SD and reopen HQ to retry. Finish a successful installation before playing.",true);
            else waitModal(ui,"Test package installed","Your local test content is ready. Remove or rename sd:/unbound/tester.zip when you no longer want HQ to offer it at startup.");
        }
    }
    bool reviewInstallOnStartup=false;
    if(contentUpdatesAllowed&&(recoveryStatus==CoreRecoveryLoadStatus::Pending||mapRepairPending)){
        reviewInstallOnStartup=confirmAction(ui,"Resume unfinished installation?",unfinishedSummary()+
            "\n\nHQ retains its exact package chain and any cached download. Review its destination before continuing.","REVIEW INSTALL");
    }else if(!testerAttempted&&contentUpdatesAllowed&&recoveryStatus==CoreRecoveryLoadStatus::Missing&&install.update.present&&install.destinationReady){
        const auto latest=latestReleaseVersion(releases,install.sku);
        const auto plan=selectedInstallPlan(releases,install,installPreferences.languageInstallPending);
        const bool behind=!latest.empty()&&(install.modVersion=="Not installed"||compareVersions(install.modVersion,latest)<0);
        const unsigned mapUpdates=installedMapUpdateCount(ui,install);
        if(shouldOpenQuickUpdates(install,installPreferences,releases,mapUpdates,installSettingsLoaded,contentUpdatesAllowed,repairNeeded()))reviewInstallOnStartup=true;
        else if((!plan.empty()||mapUpdates)&&confirmAction(ui,installPreferences.languageInstallPending?"Language installation unfinished":"Unbound installation available",
            "Installed: "+install.modVersion+"\nNewest: "+latest+"\nDestination: "+deviceName(install.update.device)+
            "\nLanguage: "+install.languageFolder+"\n"+planDescription(plan)+
            "\nSelected map packs to update: "+std::to_string(mapUpdates)+
            "\n\nReview the destination and language before downloading. Press B to continue without installing.","REVIEW INSTALL")){
            reviewInstallOnStartup=true;
        }else if(behind&&plan.empty())waitModal(ui,"Update path unavailable","No safe package path connects "+install.modVersion+" to "+latest+". Install the latest full release or report the release feed.",true);
    }
    std::vector<PluginRuntime> pluginRuntimes;
    TouchScroll newsTouchScroll;
    NetworkFeed networkFeed;
    GlobeMotion globeMotion;
    std::uint32_t globeFrame=0;
    Screen screen=Screen::Home;int detailAction=0,catalogScroll=0,detailScroll=0,newsScroll=0,newsScrollTarget=0,localScroll=0;
    WorkshopTab workshopTab=WorkshopTab::Plugins;
    DashboardNavigation dashboard;
    int pendingSection=-1;
    int catalogScrollTarget=0,detailScrollTarget=0,localScrollTarget=0,localDetailsScroll=0,localDetailsScrollTarget=0;
    NavigationRepeat navigation;
    std::size_t selected=0,localSelected=0;std::string notice,catalogNotice;std::vector<LocalModInventoryItem> localMods;GameLaunchTarget launchTarget;bool launchRequested=false,exitRequested=false,redraw=true,legacyMigrationChecked=false;
    DiagnosticsReport supportReport;
    std::string supportNotice;
    int supportScroll=0,supportScrollTarget=0;
    auto refreshSupport=[&](){
        ui.loading("Support & Stats","Reading local versions, storage and operation statistics...",-1);
        DiagnosticsContext context;
        context.install=install;
        context.installPreferences=installPreferences;
        context.contentUpdatesAllowed=contentUpdatesAllowed;
        context.modLinkEnabled=!config::ghostsMode();
        context.latestUnboundVersion=latestReleaseVersion(releases,install.sku);
        std::vector<std::string> appRecordErrors;
        const auto appRecords=parseAppReleases(sharedReleaseFeed,&appRecordErrors);
        if(appRecordErrors.empty()){
            if(const auto* newest=latestAppRelease(appRecords)){
                context.latestHqVersion=newest->version;
                context.latestHqRevision=newest->revision;
            }
        }
        context.releaseCheckStatus=updateStatus;
        context.lastError=lastSessionError;
        context.sessionWarning=startupNotices;
        context.coreRecoveryStatus=unfinishedSummary();
        context.inheritedRepairStatus=inheritedRepairGate.blocked()?inheritedRepairGate.detail():std::string{};
        context.pluginRestartRequired=pluginRestartRequired;
        context.plugins=builtInPlugins();
        for(const auto& runtime:pluginRuntimes){
            if(std::none_of(context.plugins.begin(),context.plugins.end(),[&](const auto& item){return item.key==runtime.plugin.key;}))
                context.plugins.push_back(runtime.plugin);
        }
        ProgressCallback supportProgress=[&ui](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
        runBackgroundOperation("HQ support snapshot",supportProgress,[&](const ProgressCallback& relay){
            if(relay)relay("Support & Stats","Reading bounded local information; no network requests...",-1);
            supportReport=collectDiagnostics(context);
            return true;
        });
        supportScroll=0;supportScrollTarget=0;
        supportNotice="Local information only • no network requests";
    };
    auto refreshLocalMods=[&](){
        const std::string selectedFolder=localMods.empty()?std::string{}:
            lowerKey(localMods[std::min(localSelected,localMods.size()-1)].folder);
        std::string scanError;
        if(!scanLocalModInventory(install,localMods,scanError,contentUpdatesAllowed)){
            localMods.clear();notice=scanError;
        }
        localSelected=0;
        if(!selectedFolder.empty()){
            const auto found=std::find_if(localMods.begin(),localMods.end(),[&](const auto& item){return lowerKey(item.folder)==selectedFolder;});
            if(found!=localMods.end())localSelected=static_cast<std::size_t>(std::distance(localMods.begin(),found));
        }
        if(!localMods.empty())localSelected=std::min(localSelected,localMods.size()-1);
        localScroll=std::min(localScroll,localModsMaximumScroll(localMods.size()));
        localScrollTarget=localScroll;localDetailsScroll=0;localDetailsScrollTarget=0;
    };
    auto removeConsoleMod=[&](const LocalMod& mod,std::string& removalError){
        ProgressCallback progress=[&ui,&mod](const std::string&,const std::string& detail,int percent){ui.loading("Removing "+mod.name,detail,percent);};
        return deleteInstalledMod(install,mod,removalError,progress);
    };
    auto enterLocalModManager=[&](){
        localSelected=0;localScroll=0;localScrollTarget=0;localDetailsScroll=0;localDetailsScrollTarget=0;notice.clear();
        if(!config::ghostsMode()&&!legacyMigrationChecked){
            legacyMigrationChecked=true;
            LegacyModMigrationInfo legacyInfo;std::string legacyError;
            if(!inspectLegacySdMods(legacyInfo,legacyError)){
                waitModal(ui,"Legacy mod scan failed",legacyError,true);
            }else if(legacyInfo.folderCount>0){
                const std::string detail="HQ found "+std::to_string(legacyInfo.folderCount)+
                    (legacyInfo.folderCount==1?" mod folder":" mod folders")+
                    " in the old sd:/unbound/mods or sd:/unbound/mod location. Move every non-conflicting folder into sd:/unbound/content/mods now?\n\nExisting destination folders are never overwritten. Loose files and unsafe entries remain in the old location for manual review.";
                if(!contentUpdatesAllowed){
                    notice="Legacy mods were found, but migration is unavailable until the required HQ update is installed.";
                }else if(confirmAction(ui,"Move legacy SD mods?",detail,"MIGRATE")){
                    LegacyModMigrationResult migrated;
                    if(!migrateLegacySdMods(migrated,legacyError)){
                        waitModal(ui,"Legacy migration stopped",legacyError+"\n\nAlready moved: "+std::to_string(migrated.moved)+". Nothing at the destination was overwritten.",true);
                    }else{
                        notice=std::to_string(migrated.moved)+(migrated.moved==1?" legacy mod was moved.":" legacy mods were moved.");
                        if(migrated.conflicts||migrated.skipped||!migrated.rootsStillPresent.empty()){
                            waitModal(ui,"Legacy migration finished",notice+"\n\nConflicts left in place: "+std::to_string(migrated.conflicts)+"\nOther entries left in place: "+std::to_string(migrated.skipped)+"\n\nOld folders remain only when they still contain something that HQ deliberately did not move.");
                        }
                    }
                }else notice="Legacy mod folders were left in their original location.";
            }else if(legacyInfo.otherEntryCount>0){
                notice="The old Unbound mod location contains loose or unsupported entries; HQ left them untouched.";
            }
        }
        refreshLocalMods();
        const auto duplicateCount=static_cast<unsigned>(std::count_if(localMods.begin(),localMods.end(),[](const auto& item){return item.hasSd&&item.hasConsole;}));
        if(!config::ghostsMode()&&duplicateCount&&contentUpdatesAllowed&&install.update.present){
            const std::string body="HQ found "+std::to_string(duplicateCount)+
                (duplicateCount==1?" mod folder":" mod folders")+
                " in both locations. SD files override matching console files, but console-only files can still remain visible and create a mixed or stale mod.\n\nRemove "+
                (duplicateCount==1?"the console copy":"all duplicated console copies")+
                " while preserving every SD copy?";
            if(confirmAction(ui,duplicateCount==1?"Resolve duplicate mod?":"Resolve duplicate mods?",body,duplicateCount==1?"REMOVE CONSOLE":"REMOVE ALL")){
                unsigned removed=0;std::vector<std::string> failures;
                for(const auto& item:localMods){
                    if(!item.hasSd||!item.hasConsole)continue;
                    std::string removalError;
                    if(removeConsoleMod(item.console,removalError))++removed;
                    else failures.push_back(item.console.name+": "+removalError);
                }
                refreshLocalMods();
                notice=std::to_string(removed)+(removed==1?" console duplicate was removed.":" console duplicates were removed.");
                if(!failures.empty()){
                    std::string failureText=notice+"\n\n"+std::to_string(failures.size())+" could not be removed:";
                    for(const auto& failure:failures)failureText+="\n- "+failure;
                    waitModal(ui,"Duplicate cleanup incomplete",failureText,true);
                }
            }else notice=std::to_string(duplicateCount)+(duplicateCount==1?" shared folder remains. SD matching files take priority.":" shared folders remain. SD matching files take priority.");
        }
        screen=Screen::LocalMods;redraw=true;
    };
    if(reviewInstallOnStartup){
        const auto result=openContentMenu(ui,install,automaticInstall,installPreferences,releases,contentUpdatesAllowed,installSettingsLoaded,&pendingSection);
        if(result==InstallMenuResult::Exit)exitRequested=true;
        availableMapUpdates=contentUpdatesAllowed?installedMapUpdateCount(ui,install):0;
        dashboard.returnToMenu(result==InstallMenuResult::Completed?0:1);
    }
    const auto switchGame=[&](Game game){
        if(game==config::activeGame)return;
        networkFeed.setGame(game);
        ui.invalidateTouch();
        config::activeGame=game;
        startupNotices.clear();
        startupNeedsAttention=false;
        lastSessionError.clear();
        localMods.clear();
        notice.clear();
        loadGame();
        if(!startupNotices.empty())
            waitModal(ui,"Game notices",startupNotices,startupNeedsAttention);
        screen=Screen::Home;
        dashboard.returnToMenu(0);
        if(shouldOpenQuickUpdates(install,installPreferences,releases,availableMapUpdates,installSettingsLoaded,contentUpdatesAllowed,repairNeeded())){
            networkFeed.pause();
            const auto result=openContentMenu(ui,install,automaticInstall,installPreferences,releases,contentUpdatesAllowed,installSettingsLoaded,&pendingSection);
            if(result==InstallMenuResult::Exit)exitRequested=true;
            availableMapUpdates=contentUpdatesAllowed?installedMapUpdateCount(ui,install):0;
            dashboard.returnToMenu(result==InstallMenuResult::Completed?0:1);
        }
        redraw=true;
    };
    while(WHBProcIsRunning()&&!launchRequested&&!exitRequested){
        const std::uint32_t frameStarted=SDL_GetTicks();
        std::optional<TouchAction> touch;
        const auto input=readUiInput(ui,&touch);
        const auto directions=navigation.update(input.buttons,input.held,SDL_GetTicks());
        const bool smoothPage=screen==Screen::News||screen==Screen::Support||screen==Screen::Details;
        auto b=smoothPage?input.buttons:
            (input.buttons&~(button::Up|button::Down|button::Left|button::Right))|directions;
        if(b&button::Plus){exitRequested=true;break;}
        if((screen==Screen::Home&&(b&button::L))||(touch&&touch->kind==TouchActionKind::GameChoice)){
            const Game next=touch&&touch->kind==TouchActionKind::GameChoice
                ? (touch->value==0?Game::BlackOpsII:Game::Ghosts)
                : (config::ghostsMode()?Game::BlackOpsII:Game::Ghosts);
            switchGame(next);
            continue;
        }
        if(pendingSection>=0){
            if(!(b&button::B))touch=TouchAction{TouchActionKind::Sidebar,pendingSection};
            pendingSection=-1;
        }
        if(touch){
            if(touch->kind==TouchActionKind::Sidebar&&touch->value>=0&&touch->value<=5){
                screen=Screen::Home;dashboard.returnToMenu(touch->value);b=button::A;redraw=true;
            }else if(screen==Screen::Home&&touch->kind==TouchActionKind::HomeAction&&
                     (touch->value==0||touch->value==1||touch->value==4||touch->value==5)){
                dashboard.selectAction(touch->value);b=button::A;redraw=true;
            }else if(screen==Screen::LocalMods&&touch->kind==TouchActionKind::LocalModRow&&
                     touch->value>=0&&static_cast<std::size_t>(touch->value)<localMods.size()){
                localSelected=static_cast<std::size_t>(touch->value);
                localDetailsScroll=0;localDetailsScrollTarget=0;redraw=true;
            }else if(screen==Screen::Catalog&&touch->kind==TouchActionKind::WorkshopTab&&
                     touch->value>=0&&touch->value<=int(WorkshopTab::CustomPackages)&&!(b&(button::B|button::L))){
                workshopTab=static_cast<WorkshopTab>(touch->value);
                b=0;redraw=true;
            }else if(screen==Screen::Catalog&&isWorkshopContentTab(workshopTab)&&
                     touch->kind==TouchActionKind::CustomMapRow&&touch->value>=0&&
                     static_cast<std::size_t>(touch->value)<workshopContentCatalog(workshopTab).items.size()){
                workshopContentCatalog(workshopTab).selected=static_cast<std::size_t>(touch->value);b=0;redraw=true;
            }else if(screen==Screen::Catalog&&isWorkshopContentTab(workshopTab)&&
                     touch->kind==TouchActionKind::CustomMapAction&&touch->value>=0&&
                     static_cast<std::size_t>(touch->value)<workshopContentCatalog(workshopTab).items.size()){
                workshopContentCatalog(workshopTab).selected=static_cast<std::size_t>(touch->value);b=button::A;redraw=true;
            }else if(screen==Screen::Catalog&&workshopTab==WorkshopTab::Plugins&&touch->kind==TouchActionKind::PluginRow&&
                     touch->value>=0&&static_cast<std::size_t>(touch->value)<pluginRuntimes.size()){
                selected=static_cast<std::size_t>(touch->value);b=button::A;redraw=true;
            }else if(screen==Screen::Details&&touch->kind==TouchActionKind::PluginAction&&
                     touch->value>=0&&touch->value<=1){
                detailAction=touch->value;b=button::A;redraw=true;
            }
        }
        if(screen==Screen::Home){
            if(dashboard.move(b,config::ghostsMode()?1:0))redraw=true;
            if(dashboard.gameChoice()>=0&&(b&button::A)){
                const auto game=dashboard.gameChoice()==0?Game::BlackOpsII:Game::Ghosts;
                dashboard.leaveGameSwitch();
                switchGame(game);
                redraw=true;
                continue;
            }
            if(b&button::Y){refreshSupport();screen=Screen::Support;dashboard.returnToMenu(4);redraw=true;}
            const bool activate=screen==Screen::Home&&!(b&button::B)&&
                (b&button::A);
            if(activate){
                const int homeAction=dashboard.menuFocused()?dashboard.section():dashboard.action();
                if(homeAction!=0&&homeAction!=5)networkFeed.pause();
                if(dashboard.menuFocused()&&homeAction==0){dashboard.enterContent();redraw=true;}
                else if(!dashboard.menuFocused()&&homeAction==0){screen=Screen::News;newsTouchScroll.reset();newsScroll=0;newsScrollTarget=0;redraw=true;}
                else if(homeAction==1){
                    const auto result=openContentMenu(ui,install,automaticInstall,installPreferences,releases,contentUpdatesAllowed,installSettingsLoaded,&pendingSection);
                    if(result==InstallMenuResult::Exit)exitRequested=true;
                    availableMapUpdates=contentUpdatesAllowed?installedMapUpdateCount(ui,install):0;
                    dashboard.returnToMenu(result==InstallMenuResult::Completed?0:1);redraw=true;
                }
                else if(homeAction==2){
                    std::string workshopWarning;
                    openWorkshop(ui,pluginRuntimes,workshopWarning,false);
                    {
                        if(localPackageRepairPending)workshopTab=WorkshopTab::CustomPackages;
                        sessionCustomMaps.dirty=sessionMods.dirty=true;
                        selected=0;catalogScroll=0;catalogScrollTarget=0;detailScroll=0;detailScrollTarget=0;detailAction=0;
                        notice.clear();catalogNotice=workshopWarning;screen=Screen::Catalog;
                    }
                    redraw=true;
                }
                else if(homeAction==3)enterLocalModManager();
                else if(homeAction==5){screen=Screen::Network;globeMotion.resetGesture();redraw=true;}
                else if(dashboard.menuFocused()&&homeAction==4){refreshSupport();screen=Screen::Support;redraw=true;}
                else{
                    if(!config::ghostsMode()&&testerFailed){
                        waitModal(ui,"Finish the test installation","Reopen HQ and retry tester.zip before launching the game.",true);redraw=true;
                    }else if(repairNeeded()){
                        waitModal(ui,"Repair before playing",unfinishedSummary()+
                            "\n\nOpen Install / Update and finish the required repair or installation before launching the game.",true);redraw=true;
                    }else if(!config::ghostsMode()&&retiredPluginRestartRequired){
                        waitModal(ui,"Restart your console","The old SD plugin was retired or needs attention. Fully restart the console before launching the game.",true);redraw=true;
                    }else if(!pluginRestartRequired||confirmAction(ui,"Plugin restart required",
                        "Updated plugin files are on your SD card, but Aroma still has the previous plugin code loaded. Fully restart the console/Aroma before using them. Launch without restarting?","LAUNCH ANYWAY")){
                        ui.loading(std::string("Finding ")+gameName(config::activeGame),"Checking the disc drive, USB and internal storage...",-1);
                        launchTarget=prepareGameLaunch(install);
                        launchRequested=launchTarget.titleId!=0;
                        if(!launchRequested)waitModal(ui,std::string(gameName(config::activeGame))+" not found",
                            "Insert the matching Wii U disc or reconnect the drive containing the base game, then choose Launch Game again.",true);
                    }
                    redraw=true;
                }
            }
        }else if(screen==Screen::Network){
            if(b&button::B){screen=Screen::Home;dashboard.returnToMenu(5);globeMotion.resetGesture();redraw=true;}
            else {if(globeMotion.update(input.touch,SDL_GetTicks(),true))ui.cancelTouchGesture();
                if(input.held&button::Left)globeMotion.yaw-=0.025f;
                if(input.held&button::Right)globeMotion.yaw+=0.025f;
                if(input.held&button::Up)globeMotion.pitch=std::min(1.1f,globeMotion.pitch+0.015f);
                if(input.held&button::Down)globeMotion.pitch=std::max(-1.1f,globeMotion.pitch-0.015f);}
        }else if(screen==Screen::News){
            if(b&button::B){newsTouchScroll.reset();screen=Screen::Home;dashboard.returnToMenu(0);redraw=true;}else{
                const int maximum=ui.newsMaximumScroll();
                const bool controllerScroll=(b|input.held)&(button::Up|button::Down);
                if(controllerScroll)newsTouchScroll.reset();
                const int before= newsScroll;
                const bool dragging=!controllerScroll&&newsTouchScroll.update(input.touch,SDL_GetTicks(),maximum,newsScroll);
                if(dragging){ui.cancelTouchGesture();newsScrollTarget=newsScroll;if(before!=newsScroll)redraw=true;}
                else {if(b&button::Down)newsScrollTarget=std::min(maximum,newsScrollTarget+48);if(b&button::Up)newsScrollTarget=std::max(0,newsScrollTarget-48);if(input.held&button::Down)newsScrollTarget=std::min(maximum,newsScrollTarget+8);if(input.held&button::Up)newsScrollTarget=std::max(0,newsScrollTarget-8);newsScrollTarget=std::clamp(newsScrollTarget,0,maximum);const int difference=newsScrollTarget-newsScroll;if(difference!=0){newsScroll+=std::clamp(difference,-18,18);redraw=true;}}}
        }else if(screen==Screen::Support){
            if(b&button::B){screen=Screen::Home;dashboard.returnToMenu(4);redraw=true;}
            else if(b&button::Y){refreshSupport();redraw=true;}
            else if(b&button::A){
                std::string exportError;SupportExportResult exported;
                const ProgressCallback progress=[&](const std::string& title,const std::string& detail,int percent){ui.loading(title,detail,percent);};
                if(exportSupportArchive(install,supportReport.text,exported,exportError,progress)){
                    supportNotice="Saved: sd:/wiiu/apps/Unbound-HQ/support-report.zip | "+std::to_string(exported.consoleLogs)+" console logs";
                    if(!exported.warning.empty())waitModal(ui,"Support ZIP saved",supportNotice+"\n\n"+exported.warning);
                }
                else{waitModal(ui,"Report export failed",exportError,true);supportNotice="Export failed; report remains available here.";}
                redraw=true;
            }else{
                const int maximum=ui.supportMaximumScroll();
                if(b&button::Down)supportScrollTarget=std::min(maximum,supportScrollTarget+48);
                if(b&button::Up)supportScrollTarget=std::max(0,supportScrollTarget-48);
                if(input.held&button::Down)supportScrollTarget=std::min(maximum,supportScrollTarget+8);
                if(input.held&button::Up)supportScrollTarget=std::max(0,supportScrollTarget-8);
                const int difference=supportScrollTarget-supportScroll;
                if(difference){supportScroll+=std::clamp(difference,-18,18);redraw=true;}
            }
        }else if(screen==Screen::LocalMods){
            if(b&button::B){screen=Screen::Home;dashboard.returnToMenu(3);notice.clear();redraw=true;continue;}
            const auto previousSelection=localSelected;
            if(b&button::Up){
                if(!localMods.empty()&&localSelected>0){--localSelected;redraw=true;}
            }else if(b&button::Down){
                if(!localMods.empty()){localSelected=std::min(localSelected+1,localMods.size()-1);redraw=true;}
            }
            if(localSelected!=previousSelection){localDetailsScroll=0;localDetailsScrollTarget=0;}
            const auto detailsMaximum=ui.localModDetailsMaximumScroll();
            if(input.buttons&button::Right)localDetailsScrollTarget+=48;
            else if(input.held&button::Right)localDetailsScrollTarget+=8;
            if(input.buttons&button::Left)localDetailsScrollTarget-=48;
            else if(input.held&button::Left)localDetailsScrollTarget-=8;
            localDetailsScrollTarget=std::clamp(localDetailsScrollTarget,0,detailsMaximum);
            if(!localMods.empty()){
                const int rowTop=225+int(localSelected)*82-localScrollTarget;
                if(rowTop>535)localScrollTarget=std::min(localModsMaximumScroll(localMods.size()),localScrollTarget+rowTop-535);
                if(rowTop<225)localScrollTarget=std::max(0,localScrollTarget-(225-rowTop));
                const auto item=localMods[localSelected];
                const bool sdLoaded=!config::ghostsMode();
                const auto& display=item.hasSd&&(sdLoaded||!item.hasConsole)?item.sd:item.console;
                if(b&button::A){
                    if(!contentUpdatesAllowed){
                        waitModal(ui,"HQ update required","Restart HQ and install its available application update before copying or removing mod folders.",true);
                    }else if(sdLoaded&&item.hasSd&&item.hasConsole){
                        if(confirmAction(ui,"Remove console copy of "+display.name+"?","The SD copy is preserved. Removing the console folder prevents console-only stale files from being merged with the SD version.","REMOVE CONSOLE")){
                            if(removeConsoleMod(item.console,error))notice=display.name+" now uses only the SD copy.";
                            else waitModal(ui,"Console cleanup failed",error,true);
                        }
                    }else if(item.hasSd){
                        if(!install.update.present){
                            waitModal(ui,"Official update not found","An installed game update is required before HQ can copy a mod into update/content/mods.",true);
                        }else if(confirmAction(ui,"Copy "+item.sd.name+" to console?",sdLoaded?"The game loads mods directly from SD. Copy only if you want a separate console copy.\n\nThe SD source is preserved.":"Copy this mod to the Ghosts update so the game can load it. The SD source is preserved.",sdLoaded?"COPY ANYWAY":"COPY TO CONSOLE")){
                            bool adoptionRequired=false;
                            if(!sdModRequiresOwnershipAdoption(install,item.sd,adoptionRequired,error)){
                                waitModal(ui,"Copy preflight failed",error,true);
                            }else{
                                const bool adopt=!adoptionRequired||confirmAction(ui,"Replace existing console folder?","A same-named console folder already exists but is not safely owned by HQ. HQ preserves the existing folder before replacing it. The SD source remains unchanged.","REPLACE FOLDER");
                                if(adopt){
                                    ProgressCallback copyProgress=[&ui,&item](const std::string&,const std::string& detail,int percent){ui.installLoading("Copying "+item.sd.name+" to console",detail,percent);};
                                    if(copySdModToGame(install,item.sd,error,adoptionRequired,copyProgress))
                                        notice=item.sd.name+" was copied to "+(install.update.device==Device::USB?"USB":"internal storage")+(sdLoaded?". The SD copy takes priority.":".");
                                    else waitModal(ui,"Copy failed",error,true);
                                }
                            }
                        }
                    }else if(item.hasConsole){
                        if(confirmAction(ui,(sdLoaded?"Move ":"Back up ")+item.console.name+" to SD?",
                            std::string("HQ copies and verifies the complete mod under ")+config::gameSdModsDisplayPath()+
                            (sdLoaded?". The console source is kept until you approve removal.":". The console copy remains available to Ghosts."),"COPY TO SD")){
                            ProgressCallback copyProgress=[&ui,&item](const std::string&,const std::string& detail,int percent){ui.loading("Copying "+item.console.name+" to SD",detail,percent);};
                            bool copied=copyInstalledModToSd(install,item.console,error,false,copyProgress);
                            if(!copied&&error.find("Confirm replacement before copying")!=std::string::npos&&
                               confirmAction(ui,"Replace existing SD folder?",error+"\n\nHQ will preserve the existing SD folder while the verified console copy replaces it.","REPLACE SD FOLDER"))
                                copied=copyInstalledModToSd(install,item.console,error,true,copyProgress);
                            if(!copied){
                                waitModal(ui,"Copy to SD failed",error,true);
                            }else if(sdLoaded&&confirmAction(ui,"SD copy verified","The complete mod is now on SD. Remove the original console folder so only the SD copy remains?","REMOVE ORIGINAL")){
                                if(removeConsoleMod(item.console,error))notice=item.console.name+" was moved to SD and the console original was removed.";
                                else waitModal(ui,"SD copy kept; console cleanup failed",error+"\n\nThe verified SD copy was retained.",true);
                            }else notice=item.console.name+" was copied to SD. The console original was retained.";
                        }
                    }
                    refreshLocalMods();redraw=true;
                }else if(b&button::X){
                    if(!contentUpdatesAllowed){
                        waitModal(ui,"HQ update required","Restart HQ and install its available application update before deleting mod folders.",true);
                    }else if(item.hasConsole){
                        const std::string body=item.hasSd&&sdLoaded
                            ?"This permanently removes the console folder while preserving the SD copy. This is the recommended way to resolve a shared folder and avoid mixed stale files."
                            :"This permanently removes the entire installed update/content/mods/"+item.console.folder+" folder from "+(install.update.device==Device::USB?"USB":"internal storage")+".";
                        if(confirmAction(ui,"Delete console copy of "+item.console.name+"?",body,"DELETE CONSOLE")){
                            if(removeConsoleMod(item.console,error))notice=item.console.name+" was removed from the console.";
                            else waitModal(ui,"Delete failed",error,true);
                        }
                    }else if(item.hasSd&&confirmAction(ui,"Delete SD copy of "+item.sd.name+"?",std::string("This permanently removes ")+config::gameSdModsDisplayPath()+"/"+item.sd.folder+".","DELETE SD MOD")){
                        ProgressCallback deleteProgress=[&ui,&item](const std::string&,const std::string& detail,int percent){ui.loading("Removing "+item.sd.name,detail,percent);};
                        if(deleteSdMod(item.sd,error,deleteProgress))notice=item.sd.name+" was removed from the SD card.";
                        else waitModal(ui,"Delete failed",error,true);
                    }
                    refreshLocalMods();redraw=true;
                }
            }
        }else if(screen==Screen::Catalog){
            if((b&button::B)||(b&button::L)){screen=Screen::Home;dashboard.returnToMenu(2);notice.clear();catalogNotice.clear();redraw=true;continue;}
            if(b&(button::Left|button::Right)){
                const auto next=static_cast<WorkshopTab>(std::clamp(int(workshopTab)+((b&button::Right)?1:-1),0,int(WorkshopTab::CustomPackages)));
                if(next!=workshopTab){workshopTab=next;ui.invalidateTouch();redraw=true;}
                b&=~(button::A|button::X);
            }
            if(workshopTab==WorkshopTab::CustomPackages){
                const auto result=openLocalPackages(ui,install,contentUpdatesAllowed);
                (void)navigation.update(0,result.held,SDL_GetTicks());
                if(result.exit)exitRequested=true;
                else if(result.tab>=0)workshopTab=static_cast<WorkshopTab>(result.tab);
                else{screen=Screen::Home;dashboard.returnToMenu(result.section<0?2:result.section);if(result.section>=0&&result.section!=2)pendingSection=result.section;}
                redraw=true;continue;
            }else if(isWorkshopContentTab(workshopTab)){
                auto& catalog=workshopContentCatalog(workshopTab);const bool mods=workshopTab==WorkshopTab::Mods;
                if(!catalog.loaded||catalog.dirty){
                    const bool firstLoad=!catalog.loaded;loadWorkshopContent(ui,workshopTab);refreshWorkshopContentVersions(install,workshopTab);
                    if(firstLoad){catalog.selected=0;catalog.scroll=catalog.scrollTarget=0;}
                    catalog.dirty=false;redraw=true;
                }
                if(b&button::X){
                    const auto key=catalog.selected<catalog.items.size()?catalog.items[catalog.selected].id:std::string{};
                    loadWorkshopContent(ui,workshopTab,true);refreshWorkshopContentVersions(install,workshopTab);catalog.selected=0;
                    for(std::size_t i=0;i<catalog.items.size();++i)if(catalog.items[i].id==key)catalog.selected=i;
                    catalog.scroll=catalog.scrollTarget=0;redraw=true;
                }
                if(!catalog.items.empty()){
                    catalog.selected=std::min(catalog.selected,catalog.items.size()-1);
                    if((b&button::Down)&&catalog.selected+1<catalog.items.size()){++catalog.selected;redraw=true;}
                    if((b&button::Up)&&catalog.selected>0){--catalog.selected;redraw=true;}
                    const int top=234+int(catalog.selected)*142-catalog.scrollTarget;
                    if(top>510)catalog.scrollTarget=std::min(pluginCatalogMaximumScroll(catalog.items.size()),catalog.scrollTarget+top-510);
                    if(top<234)catalog.scrollTarget=std::max(0,catalog.scrollTarget-(234-top));
                    if(b&button::A){
                        const auto& map=catalog.items[catalog.selected];
                        const auto version=catalog.selected<catalog.versions.size()?catalog.versions[catalog.selected]:std::string{};
                        const auto* release=mapPackRelease(map,version,version==map.full.version);
                        if(!contentUpdatesAllowed)waitModal(ui,"HQ update required","Install the HQ update before changing game files.",true);
                        else if(repairNeeded())waitModal(ui,"Finish the current installation","Open Install / Update and finish the saved repair or installation before adding Workshop content.",true);
                        else if(!mapPackTargetReady(install)||install.modVersion.empty()||install.modVersion=="Not installed")
                            waitModal(ui,"Install Core first","Use Install / Update to install Core on the correct USB or internal-storage game update before adding Workshop content.",true);
                        else if(!release)waitModal(ui,"Newer version installed","Your installed content is newer than this catalog entry. HQ will keep it.");
                        else if(confirmAction(ui,version==map.full.version?(mods?"Reinstall mod?":"Reinstall custom map?"):version.empty()?(mods?"Install mod?":"Install custom map?"):(mods?"Update mod?":"Update custom map?"),
                            map.name+"\n\n"+map.description+"\n\nVersion: "+release->version+"\nPackage: "+packageBytes(release->compressedBytes)+
                            "\nGame storage: "+deviceName(install.update.device)+" / "+install.languageFolder+
                            (mods?"\nSD payload: "+std::string(config::gameSdContentDisplayPath())+"\nZIP folders choose the actual destinations.":""),"INSTALL")){
                            std::string mapError;downloadWasCancelled=false;
                            if(installMapJobs(ui,install,{{map.id,map.name,*release}},mapError))waitModal(ui,mods?"Mod installed":"Custom map installed",map.name+(mods?" is installed. Enable it from the game's mod menu.":" is ready. Launch the game through HQ."));
                            else if(!downloadWasCancelled){lastSessionError=mapError;waitModal(ui,mods?"Mod install stopped":"Custom map install stopped",mapError,true);}
                            refreshWorkshopContentVersions(install,workshopTab);
                        }
                        redraw=true;
                    }
                }
            }else if(!pluginRuntimes.empty()){
                if((b&button::Down)&&selected+1<pluginRuntimes.size()){++selected;redraw=true;}
                if((b&button::Up)&&selected>0){--selected;redraw=true;}
                const int itemTop=234+int(selected)*142-catalogScrollTarget;
                if(itemTop>510)catalogScrollTarget=std::min(pluginCatalogMaximumScroll(pluginRuntimes.size()),catalogScrollTarget+itemTop-510);
                if(itemTop<234)catalogScrollTarget=std::max(0,catalogScrollTarget-(234-itemTop));
                if(b&button::X){
                    const std::string selectedKey=pluginRuntimes[selected].plugin.key;
                    std::vector<PluginRuntime> refreshed;
                    std::string refreshStatus;
                    if(openWorkshop(ui,refreshed,refreshStatus,true)){
                        pluginRuntimes=std::move(refreshed);
                        const auto found=std::find_if(pluginRuntimes.begin(),pluginRuntimes.end(),
                            [&](const PluginRuntime& item){return item.plugin.key==selectedKey;});
                        selected=found==pluginRuntimes.end()?0:
                            static_cast<std::size_t>(std::distance(pluginRuntimes.begin(),found));
                        catalogScroll=std::min(catalogScroll,pluginCatalogMaximumScroll(pluginRuntimes.size()));
                        const int refreshedTop=234+int(selected)*142-catalogScroll;
                        if(refreshedTop>510)
                            catalogScroll=std::min(pluginCatalogMaximumScroll(pluginRuntimes.size()),
                                                   catalogScroll+refreshedTop-510);
                        if(refreshedTop<234)catalogScroll=std::max(0,catalogScroll-(234-refreshedTop));
                        catalogScrollTarget=catalogScroll;
                        catalogNotice=refreshStatus;
                    }else catalogNotice=refreshStatus.empty()?"Catalog refresh failed; saved listings were kept.":refreshStatus;
                    if(catalogNotice.rfind("Could not refresh",0)==0||
                       catalogNotice.rfind("Catalog refresh failed",0)==0)
                        waitModal(ui,"Workshop refresh unavailable",catalogNotice,true);
                    redraw=true;
                }else if(b&button::A){
                    auto& runtime=pluginRuntimes[selected];
                    const std::string* prefetched=runtime.plugin.key=="UNBOUND_MOD_LINK"&&
                        !sharedReleaseFeed.empty()?&sharedReleaseFeed:nullptr;
                    refreshPluginRuntime(ui,runtime,25,contentUpdatesAllowed,prefetched);
                    screen=Screen::Details;detailAction=0;detailScroll=0;detailScrollTarget=0;notice.clear();redraw=true;
                }
            }
        }else if(screen==Screen::Details){
            if(b&button::B){screen=Screen::Catalog;notice.clear();redraw=true;continue;}
            if(pluginRuntimes.empty()){screen=Screen::Catalog;redraw=true;continue;}
            auto& runtime=pluginRuntimes[selected];
            if(directions&(button::Left|button::Right)){detailAction=(detailAction+1)%2;redraw=true;}
            if(b&button::Down)detailScrollTarget+=48;else if(input.held&button::Down)detailScrollTarget+=8;
            if(b&button::Up)detailScrollTarget-=48;else if(input.held&button::Up)detailScrollTarget-=8;
            detailScrollTarget=std::clamp(detailScrollTarget,0,ui.pluginDetailsMaximumScroll());
            if(b&button::A){
                if(detailAction==0){
                    if(!contentUpdatesAllowed)waitModal(ui,"HQ update required","Restart HQ and install its available application update before changing Aroma plugin files.",true);
                    else if(runtime.status.presence==PluginPresence::Damaged&&!runtime.status.repairable)
                        waitModal(ui,"Manual recovery required",runtime.feedStatus.empty()?"HQ cannot safely repair this plugin state automatically. Relaunch HQ once; if the warning remains, preserve the listed files and follow its recovery instructions.":runtime.feedStatus,true);
                    else if(!runtime.releaseReady)waitModal(ui,"Release unavailable",runtime.feedStatus,true);
                    else{
                        const bool unmanaged=runtime.status.presence==PluginPresence::Unmanaged;
                        const std::string prompt=unmanaged
                            ? "HQ found existing files for this plugin without an ownership receipt. Continuing keeps a backup, installs the verified release, and lets HQ manage future updates."
                            : "HQ will verify the complete package, preserve current files, and safely install every approved Aroma file. A full console/Aroma restart is required afterward.";
                        if(confirmAction(ui,unmanaged?"Replace and manage "+runtime.plugin.name+"?":"Install "+runtime.plugin.name+"?",prompt,unmanaged?"REPLACE & MANAGE":"INSTALL")){
                            if(downloadAndInstallPlugin(ui,runtime,unmanaged,error)){
                                notice=runtime.release.version+" installed. Restart the console/Aroma environment.";
                                waitModal(ui,runtime.plugin.name+" installed",notice);
                            }else waitModal(ui,"Plugin install failed",error,true);
                        }
                    }
                    redraw=true;
                }else{
                    if(!supportsStartupPluginAutoUpdate(runtime.plugin)){
                        waitModal(ui,"Auto-Update unavailable",
                            "HQ can check and install this plugin when you open its Workshop page, "
                            "but automatic startup updates are not supported for this listing yet.");
                        redraw=true;
                        continue;
                    }
                    if(runtime.plugin.key=="UNBOUND_MOD_LINK"){
                        waitModal(ui,"Automatic updates are required","HQ checks for verified Unbound Mod Link updates from the shared releases-chain-v2.txt feed. Its runtime ON/OFF switch is in Mod Manager and is independent of plugin updates.");
                        redraw=true;
                        continue;
                    }
                    const bool enable=!runtime.autoUpdateEnabled;
                    if(!contentUpdatesAllowed){
                        waitModal(ui,"HQ update required","Restart HQ and install its available application update before changing plugin settings.",true);
                    }else if(enable&&runtime.status.presence!=PluginPresence::Managed){
                        waitModal(ui,"Install first","Install or repair this plugin through HQ before enabling automatic updates.",true);
                    }else{
                        const bool accepted=!enable||confirmAction(ui,"Enable "+runtime.plugin.name+" Auto-Update?","At each HQ startup, HQ will check this plugin's developer-owned release feed. A newer revision is downloaded only after its size, archive SHA-256, exact file list, and per-file SHA-256 values pass verification. Existing unmanaged files are never updated automatically.","ENABLE");
                        if(accepted){
                            if(savePluginAutoUpdateSetting(runtime.plugin.key,enable,error)){
                                runtime.autoUpdateEnabled=enable;
                                notice=runtime.plugin.name+" Auto-Update is "+(enable?"ON.":"OFF.");
                            }else waitModal(ui,"Setting not saved",error,true);
                        }
                    }
                    redraw=true;
                }
            }
        }
        const bool globeVisible=screen==Screen::Home||screen==Screen::Network;
        if(networkFeed.tick(globeVisible,SDL_GetTicks()))redraw=true;
        if(screen==Screen::Home)globeMotion.update({},SDL_GetTicks(),false);
        if(globeVisible&&SDL_GetTicks()-globeFrame>=33){globeFrame=SDL_GetTicks();redraw=true;}
        ui.setNetwork(networkFeed.snapshot(),globeMotion.yaw,globeMotion.pitch);
        const auto easeScroll=[&redraw](int& value,int target){const int distance=target-value;if(distance){value+=std::clamp(distance,-18,18);redraw=true;}};
        if(screen==Screen::Details)easeScroll(detailScroll,detailScrollTarget);
        if(screen==Screen::Catalog&&workshopTab==WorkshopTab::Plugins)easeScroll(catalogScroll,catalogScrollTarget);
        if(screen==Screen::Catalog&&isWorkshopContentTab(workshopTab)){auto& catalog=workshopContentCatalog(workshopTab);easeScroll(catalog.scroll,catalog.scrollTarget);}
        if(screen==Screen::LocalMods){easeScroll(localScroll,localScrollTarget);easeScroll(localDetailsScroll,localDetailsScrollTarget);}
        if(redraw){
            const Release* release=releaseForSku(releases,install.sku);
            const int activeSection=screen==Screen::Network?5:screen==Screen::LocalMods?3:(screen==Screen::Support?4:
                ((screen==Screen::Catalog||screen==Screen::Details)?2:0));
            ui.setNavigation(screen==Screen::Home?dashboard.section():activeSection,screen==Screen::Home&&dashboard.menuFocused());
            if(screen==Screen::Home)ui.home(install,release,changes,dashboard.menuFocused()?-1:dashboard.action(),updateStatus,repairNeeded(),pluginRestartRequired,dashboard.gameChoice(),availableMapUpdates);
            else if(screen==Screen::Network)ui.network();
            else if(screen==Screen::News)ui.news(changes,newsScroll);
            else if(screen==Screen::Support)ui.support(supportReport.text,supportScroll,supportNotice);
            else if(screen==Screen::LocalMods)ui.localModManager(localMods,localSelected,localScroll,notice,!config::ghostsMode(),install.update.device,localDetailsScroll);
            else if(screen==Screen::Catalog){
                if(isWorkshopContentTab(workshopTab)){const auto& catalog=workshopContentCatalog(workshopTab);ui.workshopContent(workshopTab,catalog.items,catalog.versions,catalog.selected,catalog.scroll,catalog.notice,contentUpdatesAllowed&&mapPackTargetReady(install)&&!repairNeeded()&&!install.modVersion.empty()&&install.modVersion!="Not installed");}
                else{std::vector<WorkshopPlugin> plugins;std::vector<PluginInstallStatus> statuses;std::vector<bool> checked,ready;for(const auto& runtime:pluginRuntimes){plugins.push_back(runtime.plugin);statuses.push_back(runtime.status);checked.push_back(runtime.checked);ready.push_back(runtime.releaseReady);}ui.pluginCatalog(plugins,statuses,checked,selected,catalogScroll,catalogNotice,ready);}
            }
            else if(screen==Screen::Details&&!pluginRuntimes.empty()){const auto& runtime=pluginRuntimes[selected];ui.pluginDetails(runtime.plugin,runtime.releaseReady?&runtime.release:nullptr,runtime.status,runtime.autoUpdateEnabled,detailAction,detailScroll,notice,runtime.feedStatus);}
            redraw=false;
        }
        finishFrame(ui,frameStarted);
    }
    networkFeed.stop();
    if(launchRequested){
        ui.loading(std::string("Launching ")+gameName(config::activeGame),
                   std::string("Starting from ")+deviceName(launchTarget.device)+"...",100);
        shutdownPlatform();
        if(!launchTarget.path.empty())
            _SYSLaunchTitleByPathFromLauncher(launchTarget.path.c_str(),launchTarget.path.size());
        else
            SYSLaunchTitle(launchTarget.titleId);
        while(WHBProcIsRunning())waitFrame(ui);
    }else if(exitRequested){
        ui.loading("Returning to the Wii U Menu","Closing files and storage safely...",100);
        shutdownPlatform();SYSLaunchMenu();while(WHBProcIsRunning())waitFrame(ui);
    }else shutdownPlatform();
    ui.shutdown();WHBLogCafeDeinit();shutdownInput();WHBProcShutdown();return 0;
}

