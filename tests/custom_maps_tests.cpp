#include "custom_maps.hpp"
#include "package_layout.hpp"
#include <cassert>
#include <iostream>
using namespace unbound;
int main(){
    const std::string hash(64,'a');
    const std::string header="# UNBOUND_CUSTOM_MAPS_V1\n";
    const std::string catalog=header+"PACK|custom-island|10|Island|A community map\n"
        "FILE|custom-island|FULL|-|1.0.1|https://example.test/Island-v1.0.1.zip|"+hash+"|1871727718\n"
        "FILE|custom-island|PATCH|1.0.0|1.0.1|https://example.test/Island-patch.zip|"+hash+"|1000\n";
    std::vector<MapPack> maps;std::string error;
    assert(parseCustomMaps(header,maps,error)&&maps.empty());
    assert(parseCustomMaps(catalog,maps,error)&&maps.size()==1);
    assert(mapPackRelease(maps[0],"")->kind==ReleaseKind::Full);
    assert(mapPackRelease(maps[0],"1.0.0")->kind==ReleaseKind::Patch);
    assert(!mapPackRelease(maps[0],"1.0.1"));
    assert(mapPackRelease(maps[0],"1.0.1",true)->kind==ReleaseKind::Full);
    assert(!mapPackRelease(maps[0],"1.0.2",true));
    const auto marker=markerWithPack("1.0.4\nDLC|pack-1|1.0.1|english|USB\n","custom-island","1.0.1","english",Device::USB);
    assert(packVersion(marker,"pack-1","english",Device::USB)=="1.0.1");
    assert(packVersion(marker,"custom-island","english",Device::USB)=="1.0.1");
    for(const auto& bad:{catalog.substr(header.size()),header+"UNIVERSAL|FULL|-|1.0.0|x|x|1\n",header+"PACK|pack-1|10|Official DLC|Wrong namespace\nFILE|pack-1|FULL|-|1.0.0|https://example.test/dlc.zip|"+hash+"|100\n",catalog+"PACK|custom-island|10|Duplicate|Bad\n"})
        assert(!parseCustomMaps(bad,maps,error)&&maps.empty());
    const auto bo2=std::string(config::gameCustomMapsUrl());config::activeGame=Game::Ghosts;
    assert(bo2!=config::gameCustomMapsUrl());config::activeGame=Game::BlackOpsII;
    const std::string modsHeader="# UNBOUND_MODS_V1\n";
    auto modsText=catalog;
    modsText.replace(0,header.size(),modsHeader);
    for(auto pos=modsText.find("custom-island");pos!=std::string::npos;pos=modsText.find("custom-island",pos+12))modsText.replace(pos,13,"mod-survival");
    assert(parseWorkshopMods(modsHeader,maps,error)&&maps.empty());
    assert(parseWorkshopMods(modsText,maps,error)&&maps.size()==1&&maps[0].id=="mod-survival");
    assert(mapPackRelease(maps[0],"")->kind==ReleaseKind::Full);
    assert(mapPackRelease(maps[0],"1.0.0")->kind==ReleaseKind::Patch);
    assert(mapPackRelease(maps[0],"1.0.1",true)->kind==ReleaseKind::Full);
    assert(!mapPackRelease(maps[0],"1.0.2",true));
    assert(!parseCustomMaps(modsText,maps,error)&&maps.empty());
    assert(!parseWorkshopMods(catalog,maps,error)&&maps.empty());
    assert(!parseWorkshopMods(modsHeader+catalog.substr(header.size()),maps,error)&&maps.empty());
    std::string relative;
    const auto modRoute=modPackageRoute("update/content/mods/survival/modload.txt","english");
    assert(modRoute.area==PackageArea::Update&&modPackageOwnershipKey(modRoute)=="mods/survival/modload.txt");
    assert(modPackageOwnershipKey(modPackageRoute("content/mods/survival/modload.txt","english"))=="mods/survival/modload.txt");
    assert(modPackageOwnershipKey(modPackageRoute("sd/mods/survival/script.gsc","english"))=="sd:mods/survival/script.gsc");
    assert(modPackageOwnershipKey(modPackageRoute("update/code/game.rpl","english"))=="code:game.rpl");
    assert(modPackageRoute("update/meta/meta.xml","english").area==PackageArea::Invalid);
    assert(!mapPackRelativePath("update/content/mods/survival/modload.txt","english",relative));
    assert(!mapPackRelativePath("update/code/game.rpl","english",relative));
    const auto modsMarker=markerWithPack(marker,"mod-survival","1.0.1","english",Device::USB);
    assert(packVersion(modsMarker,"mod-survival","english",Device::USB)=="1.0.1");
    assert(packVersion(modsMarker,"custom-island","english",Device::USB)=="1.0.1");
    const auto bo2Mods=std::string(config::gameModsUrl());
    assert(bo2Mods!=config::gameCustomMapsUrl()&&bo2Mods!=config::gameReleasesUrl());
    config::activeGame=Game::Ghosts;
    assert(bo2Mods!=config::gameModsUrl()&&std::string(config::gameModsUrl())!=config::gameCustomMapsUrl());
    config::activeGame=Game::BlackOpsII;
    std::cout<<"Workshop content: Mods and Custom Maps catalogs, exact patches, namespace/receipt isolation, routing, reinstalls, downgrade prevention and per-game feeds passed\n";
}
