#include "package_layout.hpp"
#include "local_package_state.hpp"
#include "update_estimates.hpp"
#include <cassert>
#include <iostream>
using namespace unbound;
int main(){
    for(const auto& sku:SupportedSkus)for(auto device:{Device::USB,Device::MLC})for(const auto* language:{"english","french","german","italian","spanish","japanese"}){
        GameInstall install;install.game=sku.game;install.sku=&sku;install.languageFolder=language;
        install.update={device,stockTitleRoot(device,sku.updateTitleId),128,true};
        const auto code=packageRoute("update/code/game.rpl",language);
        assert(packageDestination(code,install,sku.game)==install.update.root+"/code/game.rpl");
        const auto localized=packageRoute("update/content/@language/file.ff",language);
        assert(packageDestination(localized,install,sku.game)==install.update.root+"/content/"+language+"/file.ff");
        const auto sd=packageRoute("sd/mods/custom/script.gsc",language);
        assert(packageDestination(sd,install,sku.game)==packageSdRoot(sku.game)+"/mods/custom/script.gsc");
        const auto movie=packageRoute("update/content/video/intro.bik",language);
        const auto target=packageDestination(movie,install,sku.game);
        assert(target.rfind(install.update.root+"/content/",0)==0);
        auto missing=install;missing.sku=nullptr;missing.update={};missing.destinationReady=false;
        assert(packageDestination(code,missing,sku.game).empty());
        assert(packageDestination(sd,missing,sku.game)==packageSdRoot(sku.game)+"/mods/custom/script.gsc");
        auto wrong=install;wrong.update.root+="-wrong";assert(packageDestination(code,wrong,sku.game).empty());
    }
    assert(packageSdRoot(Game::BlackOpsII)!=packageSdRoot(Game::Ghosts));
    for(const auto* path:{"../sd/foo","sd/../../wiiu/apps/app.wuhb","sd/file.","sd/file ","sd//file","release/update/code/game.rpl","update/meta/meta.xml","sd/update.txt","update/content/update.txt","sd/mods/x.unbound-replacing","sd/bad|name","/sd/file"})
        assert(packageRoute(path,"english").area==PackageArea::Invalid);
    assert(sdBrowsePath(SdBrowseRoot));assert(sdBrowsePath("fs:/vol/external01/a folder/package.ZIP"));
    for(const auto* path:{"fs:/vol/external01/../x","storage_usb01:/x.zip","fs:/vol/external01-bad/x","fs:/vol/external01/folder/","fs:/vol/external01/a|b"})assert(!sdBrowsePath(path));
    assert(zipFilename("Mod.ZIP")&&!zipFilename("Mod.zip.bak"));
    GameInstall install;install.game=Game::BlackOpsII;install.sku=&SupportedSkus[0];install.languageFolder="english";
    install.update={Device::USB,stockTitleRoot(Device::USB,install.sku->updateTitleId),128,true};
    LocalPackageInfo info{"fs:/vol/external01/Packages/Custom.zip",std::string(64,'a'),100,50,40,1,1};
    LocalPackagePending pending;std::string error;const auto text=localPackageJournal(info,install);
    assert(parseLocalPackageJournal(text,pending,error)&&sameLocalPackageTarget(pending,install)&&sameLocalPackageSource(pending,info));
    auto moved=install;moved.languageFolder="french";assert(!sameLocalPackageTarget(pending,moved));
    moved=install;moved.update.device=Device::MLC;assert(!sameLocalPackageTarget(pending,moved));
    auto changed=info;changed.sha256=std::string(64,'b');assert(!sameLocalPackageSource(pending,changed));
    info.updateFiles=0;install.sku=nullptr;install.update={};
    assert(parseLocalPackageJournal(localPackageJournal(info,install),pending,error)&&sameLocalPackageTarget(pending,install));
    assert(!parseLocalPackageJournal("broken",pending,error));
    UpdateTimingHistory history;
    history.read("r64|download|ok=1|network_bytes=104857600|total_ms=100000\n"
                 "r65|install|ok=1|compressed_bytes=209715200|total_ms=100000|device=USB\n"
                 "r65|install|ok=1|compressed_bytes=52428800|total_ms=100000|device=NAND\n"
                 "r65|install|ok=0|compressed_bytes=52428800|total_ms=1000|device=USB\n");
    assert(history.download==1048576&&history.usb==2097152&&history.nand==524288);
    assert(UpdateTimingHistory::seconds(104857600,history.download,1)==100);
    assert(UpdateTimingHistory::seconds(0,history.download,1)==0);
    assert(updateDuration(59)=="<1 min"&&updateDuration(61)=="2 min");
    std::cout<<"Package routing: every game/region/language/device, SD isolation, rejected paths, browser bounds, exact recovery identity and learned estimates passed\n";
}
