#pragma once
#include "map_packs.hpp"

namespace unbound {
inline bool parseWorkshopContent(const std::string& text,std::vector<MapPack>& maps,std::string& error,bool mods){
    maps.clear();error.clear();std::istringstream lines(text);std::string line;bool header=false;
    const std::string catalogHeader=mods?"# UNBOUND_MODS_V1":"# UNBOUND_CUSTOM_MAPS_V1";
    const std::string prefix=mods?"mod-":"custom-";
    while(std::getline(lines,line)){
        if(line.rfind("\xef\xbb\xbf",0)==0)line.erase(0,3);
        line=trim(line);
        if(line==catalogHeader)header=true;
        if(!line.empty()&&line[0]!='#'&&line.rfind("PACK|",0)!=0&&line.rfind("FILE|",0)!=0){
            error="Workshop content catalogs accept only PACK and FILE records.";return false;
        }
    }
    if(!header){error="The workshop catalog header is missing: "+catalogHeader;return false;}
    if(!parseMapPacks(text,maps,error))return false;
    for(const auto& map:maps)if(map.id.rfind(prefix,0)!=0||map.id.size()==prefix.size()){
        maps.clear();error="Workshop IDs must start with "+prefix+" and include a unique name.";return false;
    }
    return true;
}
inline bool parseCustomMaps(const std::string& text,std::vector<MapPack>& maps,std::string& error){
    return parseWorkshopContent(text,maps,error,false);
}
inline bool parseWorkshopMods(const std::string& text,std::vector<MapPack>& mods,std::string& error){
    return parseWorkshopContent(text,mods,error,true);
}
}
