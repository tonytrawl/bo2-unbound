#pragma once

#include "install_selection.hpp"
#include "map_packs.hpp"
#include "update_estimates.hpp"
#include <optional>

namespace unbound {
struct QuickUpdateRow {
    std::string title, installed, latest, detail;
    std::uint64_t bytes = 0;
    std::optional<std::uint64_t> networkBytes={};
    std::uint64_t downloadSeconds=0,installSeconds=0;
    bool timingFromHistory=false;
};
struct QuickMapUpdate { std::string id,name; Release release; };
struct QuickMapState { const MapPack* pack; std::string installed; };
struct QuickUpdatePlan {
    GameInstall target;
    std::vector<const Release*> core;
    std::vector<QuickMapUpdate> maps;
    std::vector<QuickUpdateRow> rows;
    bool coreCurrent = false;
    bool empty() const { return rows.empty(); }
};

inline bool quickUpdateEligible(const GameInstall& install,const InstallPreferences& preferences,
                                bool settingsLoaded,bool contentAllowed,bool needsRepair) {
    return settingsLoaded&&contentAllowed&&!needsRepair&&!preferences.languageInstallPending&&
        install.sku&&install.game==install.sku->game&&mapPackTargetReady(install)&&
        officialUpdateStatus(install)==OfficialUpdateStatus::Current&&
        !install.modVersion.empty()&&install.modVersion!="Not installed"&&install.modVersion!="0.0.0";
}

inline QuickUpdatePlan quickUpdatePlan(const std::vector<Release>& releases,const GameInstall& install,
                                       const std::vector<QuickMapState>& maps,const std::string& minimumCore) {
    QuickUpdatePlan plan;plan.target=install;
    plan.core=selectedInstallPlan(releases,install,false);
    if(!plan.core.empty()){
        std::uint64_t bytes=0;for(const auto* release:plan.core)bytes+=release->compressedBytes;
        const bool patch=std::all_of(plan.core.begin(),plan.core.end(),[](const Release* release){return release->kind==ReleaseKind::Patch;});
        plan.rows.push_back({install.game==Game::Ghosts?"Ghosts Core":"Unbound Core",install.modVersion,
                             plan.core.back()->version,patch?"Core patch":"Complete Core update",bytes});
    }
    const auto effective=plan.core.empty()?install.modVersion:plan.core.back()->version;
    const bool mapsAllowed=minimumCore.empty()||compareVersions(effective,minimumCore)>=0;
    for(const auto& state:maps){
        if(!mapsAllowed||!state.pack||state.installed.empty())continue;
        if(const auto* release=mapPackRelease(*state.pack,state.installed)){
            plan.maps.push_back({state.pack->id,state.pack->name,*release});
            plan.rows.push_back({state.pack->name,state.installed.empty()?"Not installed":state.installed,
                                 release->version,release->kind==ReleaseKind::Patch?"Map pack patch":"Full map pack",release->compressedBytes});
        }
    }
    const auto latest=latestReleaseVersion(releases,install.sku);
    plan.coreCurrent=plan.core.empty()&&!latest.empty()&&compareVersions(install.modVersion,latest)>=0;
    return plan;
}

inline bool shouldOpenQuickUpdates(const GameInstall& install,const InstallPreferences& preferences,
                                    const std::vector<Release>& releases,unsigned mapUpdates,
                                    bool settingsLoaded,bool contentAllowed,bool needsRepair) {
    return quickUpdateEligible(install,preferences,settingsLoaded,contentAllowed,needsRepair)&&
        (!selectedInstallPlan(releases,install,false).empty()||mapUpdates>0);
}

inline bool sameQuickUpdateTarget(const GameInstall& left,const GameInstall& right) {
    return left.sku&&right.sku&&left.game==right.game&&left.sku->updateTitleId==right.sku->updateTitleId&&
        left.update.device==right.update.device&&left.update.root==right.update.root&&
        left.languageFolder==right.languageFolder&&left.modVersion==right.modVersion&&right.destinationReady&&right.update.present;
}
inline bool sameQuickRelease(const Release& left,const Release& right) {
    return left.regionKey==right.regionKey&&left.kind==right.kind&&left.fromVersion==right.fromVersion&&
        left.version==right.version&&left.downloadUrl==right.downloadUrl&&left.sha256==right.sha256&&left.compressedBytes==right.compressedBytes;
}
inline bool sameQuickUpdatePlan(const QuickUpdatePlan& left,const QuickUpdatePlan& right) {
    if(!sameQuickUpdateTarget(left.target,right.target)||left.core.size()!=right.core.size()||left.maps.size()!=right.maps.size())return false;
    for(std::size_t i=0;i<left.core.size();++i)if(!sameQuickRelease(*left.core[i],*right.core[i]))return false;
    for(std::size_t i=0;i<left.maps.size();++i)
        if(left.maps[i].id!=right.maps[i].id||!sameQuickRelease(left.maps[i].release,right.maps[i].release))return false;
    return true;
}
inline int quickUpdateMaximumScroll(std::size_t rows) { return std::max(0,int(rows)*88-229); }
}
