#include "quick_update.hpp"
#include <cassert>
#include <iostream>
using namespace unbound;

int main(){
    const std::string hash(64,'a');
    const auto releases=parseReleases("UNIVERSAL|FULL|-|1.0.0|https://example.test/core.zip|"+hash+"|1000\n"
        "UNIVERSAL|PATCH|1.0.0|1.0.2|https://example.test/core-patch.zip|"+hash+"|10\n");
    MapPack pack{"pack-1","DLC Pack 1","",10,{"UNIVERSAL",ReleaseKind::Full,"-","1.0.1","https://example.test/maps.zip",hash,500},
        {{"UNIVERSAL",ReleaseKind::Patch,"1.0.0","1.0.1","https://example.test/maps-patch.zip",hash,50}}};
    GameInstall install;install.sku=&SupportedSkus[0];install.modVersion="1.0.0";
    install.update={Device::USB,"storage_usb01:/usr/title/0005000e/1010ec00",128,true};
    InstallPreferences preferences;
    assert(quickUpdateEligible(install,preferences,true,true,false));
    assert(shouldOpenQuickUpdates(install,preferences,releases,0,true,true,false));
    auto both=quickUpdatePlan(releases,install,{{&pack,"1.0.0"}},"1.0.0");
    assert(both.core.size()==1&&both.core[0]->kind==ReleaseKind::Patch&&both.core[0]->version=="1.0.2");
    assert(both.maps.size()==1&&both.maps[0].release.kind==ReleaseKind::Patch&&both.maps[0].release.compressedBytes==50);
    assert(both.rows.size()==2&&both.rows[0].bytes==10&&both.rows[1].bytes==50&&!both.coreCurrent);
    assert(sameQuickUpdatePlan(both,both));

    // Already-current Core never becomes a full reinstall for a map-only update.
    install.modVersion="1.0.2";
    auto maps=quickUpdatePlan(releases,install,{{&pack,"1.0.0"}},"1.0.0");
    assert(maps.core.empty()&&maps.maps.size()==1&&maps.rows.size()==1&&maps.coreCurrent);
    assert(shouldOpenQuickUpdates(install,preferences,releases,1,true,true,false));
    assert(!shouldOpenQuickUpdates(install,preferences,releases,0,true,true,false));
    assert(quickUpdatePlan(releases,install,{{&pack,"1.0.1"}},"1.0.0").empty());
    auto added=quickUpdatePlan(releases,install,{{&pack,""}},"1.0.0");
    assert(added.empty()); // New optional packs stay in the initial/optional installer.
    assert(!sameQuickUpdatePlan(maps,added));

    // Fresh installs, language replay and recovery retain the detailed flow.
    for(const std::string& missing:{std::string{},std::string("Not installed"),std::string("0.0.0")}){
        auto value=install;value.modVersion=missing;
        assert(!shouldOpenQuickUpdates(value,preferences,releases,1,true,true,false));
    }
    preferences.languageInstallPending=true;
    assert(!shouldOpenQuickUpdates(install,preferences,releases,1,true,true,false));
    preferences.languageInstallPending=false;
    assert(!shouldOpenQuickUpdates(install,preferences,releases,1,false,true,false));
    assert(!shouldOpenQuickUpdates(install,preferences,releases,1,true,false,false));
    assert(!shouldOpenQuickUpdates(install,preferences,releases,1,true,true,true));
    auto invalid=install;invalid.destinationReady=false;assert(!quickUpdateEligible(invalid,preferences,true,true,false));
    invalid=install;invalid.update.device=Device::Disc;assert(!quickUpdateEligible(invalid,preferences,true,true,false));
    invalid=install;invalid.update.titleVersion=1;assert(!quickUpdateEligible(invalid,preferences,true,true,false));

    // The displayed destination and package identity must survive revalidation.
    auto changed=install;changed.update.device=Device::MLC;assert(!sameQuickUpdateTarget(install,changed));
    changed=install;changed.update.root+="-different";assert(!sameQuickUpdateTarget(install,changed));
    changed=install;changed.languageFolder="french";assert(!sameQuickUpdateTarget(install,changed));
    changed=install;changed.modVersion="1.0.3";assert(!sameQuickUpdateTarget(install,changed));
    auto changedPlan=maps;changedPlan.maps[0].release.sha256=std::string(64,'b');assert(!sameQuickUpdatePlan(maps,changedPlan));
    changedPlan=maps;changedPlan.maps[0].release.compressedBytes++;assert(!sameQuickUpdatePlan(maps,changedPlan));

    const auto ghostReleases=parseReleases("UNIVERSAL|FULL|-|1.0.1|https://example.test/ghosts.zip|"+hash+"|70\n");
    for(const auto& sku:GhostsSkus){
        auto ghosts=install;ghosts.game=Game::Ghosts;ghosts.sku=&sku;ghosts.modVersion="1.0.0";ghosts.update.titleVersion=112;
        assert(shouldOpenQuickUpdates(ghosts,preferences,ghostReleases,0,true,true,false));
        auto plan=quickUpdatePlan(ghostReleases,ghosts,{},"");assert(plan.core.size()==1&&plan.rows.size()==1&&plan.rows[0].title=="Ghosts Core");
    }
    assert(quickUpdateMaximumScroll(1)==0&&quickUpdateMaximumScroll(2)==0&&quickUpdateMaximumScroll(33)>0);
    std::cout<<"Quick updates: startup routing, Core/map-only/both plans, installed-only updates, recovery gates, unchanged target, exact package identity and all Ghosts regions passed\n";
}
