#include "map_packs.hpp"
#include <cassert>
#include <iostream>
using namespace unbound;
int main(){
    assert(mapPackSelected("", "new-pack"));
    assert(mapPackSelected("pack-1|0\n", "new-pack"));
    assert(!mapPackSelected("pack-1|0\n", "pack-1"));
    assert(mapPackSelected("pack-1|1\n", "pack-1"));
    assert(!mapPackSelected("pack-1|1\npack-1|0\n", "pack-1"));
    assert(mapPackSelected("pack-1|invalid\n", "pack-1"));

    assert(nativeSdCoreVersion("#CORE_SD|1.0.0\n")=="1.0.0");
    assert(nativeSdCoreVersion("#CORE_SD|1.0.0\n#CORE_SD|2.0.0\n").empty());
    assert(nativeSdCoreVersion("#CORE_SD|broken\n").empty());
    const std::string hash(64,'a');
    const std::string catalog="# UNBOUND_MAP_PACKS_V1\nPACK|pack-2|20|Second|Second pack\nPACK|pack-1|10|First|First pack\nFILE|pack-1|FULL|-|1.2.0|https://downloads.unc-bo2.fun/one.zip|"+hash+"|100\nFILE|pack-1|PATCH|1.1.0|1.2.0|https://downloads.unc-bo2.fun/patch.zip|"+hash+"|20\nFILE|pack-2|FULL|-|1.0.0|https://downloads.unc-bo2.fun/two.zip|"+hash+"|100\n";
    std::vector<MapPack> packs;std::string error;
    assert(parseMapPacks(catalog,packs,error)&&packs.size()==2&&packs[0].id=="pack-1");
    const auto unified=std::string("# UNBOUND_RELEASE_MANIFEST_V1\n#CORE_SD|1.0.0\nUNIVERSAL|FULL|-|1.0.0|https://example.test/core.zip|")+hash+"|100\n"+catalog;
    assert(parseMapPacks(unified,packs,error)&&packs.size()==2);
    // Real Uprising package, signed-32-bit boundary and FAT32 ceiling must all
    // survive catalog parsing and release selection without narrowing sizes.
    const auto largeCatalog=[&](std::uint64_t bytes,bool patch=false){
        std::string text="PACK|pack-2|20|Unbound DLC 2: Uprising|Uprising maps\n";
        text+="FILE|pack-2|FULL|-|1.0.1|https://downloads.unc-bo2.fun/uprising.zip|"+hash+"|"+std::to_string(bytes)+"\n";
        if(patch)text+="FILE|pack-2|PATCH|1.0.0|1.0.1|https://downloads.unc-bo2.fun/patch.zip|"+hash+"|"+std::to_string(bytes)+"\n";
        return text;
    };
    for(const auto bytes:{1871727718ULL,2147483648ULL,config::MaxArchiveBytes}){
        assert(parseMapPacks(largeCatalog(bytes,true),packs,error)&&packs.size()==1);
        assert(mapPackRelease(packs[0],"")->compressedBytes==bytes);
        assert(mapPackRelease(packs[0],"1.0.0")->compressedBytes==bytes);
        std::vector<std::string> sizeErrors;
        const auto saved=parseReleases(releaseRecord(packs[0].full),&sizeErrors);
        assert(sizeErrors.empty()&&saved.size()==1&&saved[0].compressedBytes==bytes);
    }
    for(const auto bytes:{config::MaxArchiveBytes+1,~0ULL})
        assert(!parseMapPacks(largeCatalog(bytes),packs,error)&&packs.empty());
    assert(parseMapPacks(unified,packs,error)&&packs.size()==2);
    std::vector<std::string> combinedErrors;
    auto core=parseReleases(unified,&combinedErrors);
    assert(combinedErrors.empty()&&core.size()==1&&core[0].version=="1.0.0");
    assert(mapPackRelease(packs[0],"1.1.0")->kind==ReleaseKind::Patch);
    assert(mapPackRelease(packs[0],"")->kind==ReleaseKind::Full);
    assert(mapPackRelease(packs[0],"1.0.0")->kind==ReleaseKind::Full);
    assert(!mapPackRelease(packs[0],"1.2.0"));assert(!mapPackRelease(packs[0],"2.0.0"));
    assert(mapPackRelease(packs[0],"1.2.0",true)->kind==ReleaseKind::Full);
    auto marker=markerWithPack("1.0.0\n","pack-1","1.1.0","italian",Device::USB);
    marker=markerWithPack(marker,"pack-2","1.0.0","english",Device::MLC);
    marker=markerWithCore(marker,"1.0.1");
    assert(coreMarkerVersion(marker)=="1.0.1");
    assert(packVersion(marker,"pack-1","italian",Device::USB)=="1.1.0");
    assert(packVersion(marker,"pack-1","english",Device::USB).empty());
    marker=markerWithPack(marker,"pack-1","1.2.0","italian",Device::USB);
    assert(packVersion(marker,"pack-1","italian",Device::USB)=="1.2.0");
    assert(packVersion(marker,"pack-2","english",Device::MLC)=="1.0.0");
    assert(marker.find("1.1.0")==std::string::npos);
    assert(packVersion("1.0.0\nDLC|pack-1|1.1.0|italian\n","pack-1","italian",Device::MLC).empty());
    assert(packVersion("1.0.0\nDLC|pack-1|1.1.0|italian|SD\n","pack-1","italian",Device::USB).empty());
    assert(packVersion(marker,"pack-1","italian",Device::MLC).empty());
    assert(mapReceiptPath("maps","pack-1","italian",Device::USB)=="maps/pack-1-italian-usb.txt");
    assert(mapReceiptPath("maps","pack-1","italian",Device::MLC)=="maps/pack-1-italian-nand.txt");
    GameInstall game{};
    game.sku=&SupportedSkus[0];
    game.base.present=true;game.update.present=true;game.destinationReady=true;
    for(auto base:{Device::USB,Device::MLC,Device::Disc}){
        game.base.device=base;
        for(auto update:{Device::USB,Device::MLC}){
            game.update.device=update;
            assert(mapPackTargetReady(game));
        }
    }
    game.update.device=Device::Disc;assert(!mapPackTargetReady(game));
    game.update.device=Device::USB;game.destinationReady=false;assert(!mapPackTargetReady(game));
    game.destinationReady=true;game.update.present=false;assert(!mapPackTargetReady(game));
    std::string path;
    assert(mapPackRelativePath("update/content/@language/test.ff","italian",path)&&path=="italian/test.ff");
    assert(mapPackRelativePath("content/video/intro.bik","english",path)&&path=="video/intro.bik");
    for(const auto* bad:{"update/code/game.rpl","meta/meta.xml","content/../code/game.rpl","content/mods/test/modload.txt","content/MODS/test.txt","content/update.txt","content/UPDATE.TXT","/content/file","content/file:bad","content/test.ff.","content/test.ff ","content/bad?.ff"})assert(!mapPackRelativePath(bad,"english",path));
    for(const auto& bad:{catalog+"PACK|pack-1|0|Duplicate|Bad\n",catalog+"UNKNOWN|pack-1|bad\n",std::string("PACK|../bad|0|Name|Description\n"),std::string("PACK|one|0|Name|Description\n")})assert(!parseMapPacks(bad,packs,error)&&packs.empty());
    assert(parseMapPacks(catalog,packs,error));
    std::vector<std::string> errors;auto roundtrip=parseReleases(releaseRecord(packs[0].patches[0]),&errors);
    assert(errors.empty()&&roundtrip.size()==1&&roundtrip[0].sha256==hash&&roundtrip[0].fromVersion=="1.1.0");
    std::cout<<"Map catalogs, exact patches, console paths and component markers passed\n";
}
