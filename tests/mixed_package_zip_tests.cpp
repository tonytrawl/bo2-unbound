#include "package_layout.hpp"
#include "buffered_zip.hpp"
#include "install_stream.hpp"
#include "sha256.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
using namespace unbound;
namespace fs=std::filesystem;
int main(int argc,char** argv){
    assert(argc==3||argc==4);const fs::path archive=fs::absolute(argv[1]);const fs::path root=fs::absolute(argv[2]);
    fs::create_directories(root);fs::current_path(root);const bool reject=argc==4;
    // Preserve the native routing assertions, but translate volume punctuation
    // for the fixture filesystem hosted on Windows D: (which forbids colons).
    const auto host=[](std::string path){std::replace(path.begin(),path.end(),':','_');return path;};
    for(const auto& sku:SupportedSkus)for(auto device:{Device::USB,Device::MLC}){
        GameInstall install;install.game=sku.game;install.sku=&sku;install.languageFolder=sku.region==Region::Japan?"japanese":"english";
        install.update={device,stockTitleRoot(device,sku.updateTitleId),128,true};
        BufferedZipReader reader;assert(reader.open(archive.string().c_str()));auto* zip=reader.get();
        struct Entry { zip_uint64_t index;std::string target;zip_uint64_t bytes; };
        std::vector<Entry> plan;std::set<std::string> targets;bool invalid=false;
        const auto count=zip_get_num_entries(zip,0);assert(count>0);
        for(zip_uint64_t i=0;i<static_cast<zip_uint64_t>(count);++i){
            zip_stat_t entry{};assert(zip_stat_index(zip,i,0,&entry)==0);
            std::string name=entry.name;const auto route=packageRoute(name,install.languageFolder);
            const auto target=packageDestination(route,install,sku.game);
            const auto modRoute=modPackageRoute(name,install.languageFolder);
            assert(packageDestination(modRoute,install,sku.game)==target);
            if(!target.empty())assert(!modPackageOwnershipKey(modRoute).empty());
            if(target.empty()||!targets.insert(target).second){invalid=true;break;}
            plan.push_back({i,host(target),entry.size});
        }
        if(reject){assert(invalid&&!fs::exists("storage_usb01_")&&!fs::exists("fs_"));assert(reader.close());std::cout<<"Mixed ZIP unsafe path rejected before any target files were written\n";return 0;}
        assert(!invalid&&plan.size()==4);
        AlignedIoBuffer buffer;assert(buffer);
        for(const auto& entry:plan){
            fs::create_directories(fs::path(entry.target).parent_path());
            auto* input=zip_fopen_index(zip,entry.index,0);assert(input);
            const auto stage=entry.target+".unbound-new";auto* output=std::fopen(stage.c_str(),"wb");assert(output);
            OperationMetrics metrics;Sha256 hash;
            const auto status=streamInstallEntry(entry.bytes,buffer.data(),buffer.size(),
                [input](char* b,std::size_t n){return zip_fread(input,b,n);},
                [output,&hash](const char* b,std::size_t n){hash.update(b,n);return std::fwrite(b,1,n,output);},
                [](std::size_t){},metrics);
            assert(status==InstallStreamResult::Success&&zip_fclose(input)==0&&std::fclose(output)==0);
            std::string error;assert(commitInstallReplacement(stage,entry.target,error));
            assert(fs::file_size(entry.target)==entry.bytes);
            std::ifstream saved(entry.target,std::ios::binary);std::string content((std::istreambuf_iterator<char>(saved)),{});Sha256 readback;readback.update(content.data(),content.size());assert(readback.finish()==hash.finish());
        }
        assert(reader.close());
        assert(fs::exists(host(install.update.root+"/code/game.rpl")));
        assert(fs::exists(host(install.update.root+"/content/"+install.languageFolder+"/payload.ff")));
        assert(fs::exists(host(packageSdRoot(sku.game)+"/mods/demo/data.bin")));
        assert(fs::exists(host(packageSdRoot(sku.game)+"/textures/payload.iwi")));
    }
    std::cout<<"Stored/Deflate mixed ZIPs: real routing, buffered ZIP reader, CRC, streamed write, atomic replacement and readback hashes passed for every game/region/storage\n";
}
