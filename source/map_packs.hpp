#pragma once
#include "model.hpp"
#include "config.hpp"
#include <algorithm>
#include <map>
#include <sstream>
#include <set>

namespace unbound {
inline const char* mapStorageTag(Device device) {
    if (device==Device::USB) return "USB";
    if (device==Device::MLC) return "NAND";
    return "";
}
inline bool mapPackTargetReady(const GameInstall& game) {
    return game.sku && game.update.present && game.destinationReady &&
           *mapStorageTag(game.update.device);
}
inline std::string mapReceiptPath(const std::string& stateRoot,const std::string& id,
                                  const std::string& language,Device device) {
    const char* suffix=device==Device::USB?"-usb.txt":"-nand.txt";
    return stateRoot+"/"+id+"-"+language+suffix;
}
inline const char* mapOwnershipSuffix(Device device) {
    return device==Device::USB?"~usb.files":"~nand.files";
}
struct MapPack {
    std::string id, name, description;
    unsigned order=0;
    Release full;
    std::vector<Release> patches;
};
inline std::vector<std::string> packFields(const std::string& text) {
    std::vector<std::string> out; std::istringstream in(text); std::string part;
    while(std::getline(in,part,'|'))out.push_back(part);
    return out;
}
// New packs are selected by default. An explicit saved opt-out always wins.
inline bool mapPackSelected(const std::string& preferences,const std::string& id) {
    bool selected=true;std::istringstream lines(preferences);std::string line;
    while(std::getline(lines,line)){
        const auto fields=packFields(trim(line));
        if(fields.size()==2&&fields[0]==id&&(fields[1]=="0"||fields[1]=="1"))selected=fields[1]=="1";
    }
    return selected;
}
inline bool packId(const std::string& s) {
    return !s.empty()&&s.size()<=64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';});
}
// One Core capability declaration, not a dependency matrix for individual packs.
inline std::string nativeSdCoreVersion(const std::string& feed){
    std::istringstream in(feed);std::string line,result;
    while(std::getline(in,line)){
        line=trim(line);if(line.rfind("#CORE_SD|",0)!=0)continue;
        const auto f=packFields(line);
        if(!result.empty()||f.size()!=2)return {};
        std::vector<std::string> errors;
        if(parseReleases("UNIVERSAL|FULL|-|"+f[1]+"|https://example.invalid/core.zip|"+std::string(64,'0')+"|1",&errors).size()!=1||!errors.empty())return {};
        result=f[1];
    }
    return result;
}
inline std::string coreMarkerVersion(const std::string& text) { return trim(text.substr(0,text.find_first_of("\r\n"))); }
inline std::string markerWithCore(const std::string& text,const std::string& version) {
    auto end=text.find('\n'); return version+"\n"+(end==std::string::npos?"":text.substr(end+1));
}
inline std::string packVersion(const std::string& text,const std::string& id,const std::string& language,Device device) {
    if(!*mapStorageTag(device))return {};
    std::istringstream in(text);std::string line;
    while(std::getline(in,line)){
        const auto f=packFields(trim(line));
        if(f.size()==5&&f[0]=="DLC"&&f[1]==id&&f[3]==language&&f[4]==mapStorageTag(device))
            return f[2];
    }
    return {};
}
inline std::string markerWithPack(const std::string& text,const std::string& id,const std::string& version,const std::string& language,Device device) {
    std::istringstream in(text);std::string line;std::getline(in,line);std::string out=trim(line)+"\n";
    while(std::getline(in,line)){auto f=packFields(trim(line));if((f.size()==4||f.size()==5)&&f[0]=="DLC"&&f[1]==id&&f[3]==language)continue;out+=line+"\n";}
    return out+"DLC|"+id+"|"+version+"|"+language+"|"+mapStorageTag(device)+"\n";
}
inline std::string releaseRecord(const Release& r) {
    return "UNIVERSAL|"+std::string(r.kind==ReleaseKind::Full?"FULL":"PATCH")+"|"+(r.kind==ReleaseKind::Full?"-":r.fromVersion)+"|"+r.version+"|"+r.downloadUrl+"|"+r.sha256+"|"+std::to_string(r.compressedBytes);
}
inline bool parseMapPacks(const std::string& text,std::vector<MapPack>& packs,std::string& error) {
    packs.clear();std::map<std::string,MapPack> all;std::set<std::string> descriptions,full;
    std::istringstream in(text);std::string line;unsigned n=0;
    auto bad=[&](){packs.clear();error="Map catalog has an invalid or duplicate record on line "+std::to_string(n)+".";return false;};
    while(std::getline(in,line)){
        ++n;if(line.rfind("\xef\xbb\xbf",0)==0)line.erase(0,3);line=trim(line);if(line.empty()||line[0]=='#')continue;
        auto f=packFields(line);
        if(!f.empty()&&f[0]=="UNIVERSAL")continue;
        if(f.size()<2||!packId(f[1]))return bad();
        if(f[0]=="PACK"){
            if(f.size()!=5||!descriptions.insert(f[1]).second||f[2].empty()||f[2].size()>5||f[3].empty()||f[3].size()>100||f[4].size()>1800)return bad();
            unsigned order=0;for(char c:f[2]){if(c<'0'||c>'9')return bad();order=order*10+unsigned(c-'0');}
            auto& p=all[f[1]];p.id=f[1];p.order=order;p.name=f[3];p.description=f[4];
        }else if(f[0]=="FILE"){
            if(f.size()!=8)return bad();
            std::string row="UNIVERSAL";for(std::size_t i=2;i<f.size();++i)row+='|'+f[i];
            std::vector<std::string> errors;auto releases=parseReleases(row,&errors);
            if(!errors.empty()||releases.size()!=1||releases[0].compressedBytes>config::MaxArchiveBytes)return bad();
            auto& p=all[f[1]];auto r=releases[0];
            if(r.kind==ReleaseKind::Full){if(!full.insert(f[1]).second)return bad();p.full=r;}
            else {for(const auto& previous:p.patches)if(previous.fromVersion==r.fromVersion)return bad();p.patches.push_back(r);}
        }else return bad();
        if(all.size()>32)return bad();
    }
    for(auto& [id,p]:all){
        if(!descriptions.count(id)||!full.count(id))return bad();
        for(const auto& r:p.patches)if(r.version!=p.full.version)return bad();
        packs.push_back(p);
    }
    std::sort(packs.begin(),packs.end(),[](const MapPack& a,const MapPack& b){return a.order==b.order?a.id<b.id:a.order<b.order;});
    return true;
}
inline const Release* mapPackRelease(const MapPack& p,const std::string& installed,bool repair=false) {
    if(!installed.empty()&&compareVersions(installed,p.full.version)>0)return nullptr;
    if(!repair&&!installed.empty()&&compareVersions(installed,p.full.version)==0)return nullptr;
    if(!repair)for(const auto& patch:p.patches)if(patch.fromVersion==installed)return &patch;
    return &p.full;
}
// Only content is accepted. No code, metadata, mods or installer state in DLC.
inline bool mapPackRelativePath(const std::string& name,const std::string& language,std::string& relative) {
    std::string path=name;if(path.rfind("update/",0)==0)path=path.substr(7);
    if(path=="update"||path=="content"){relative.clear();return true;}
    if(path.rfind("content/",0)!=0)return false;
    std::string localized;if(!localizeArchivePath(path,language,localized))return false;
    relative=localized.substr(8);
    std::string folded=relative;std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(!safeRelativePath(relative)||folded=="mods"||folded.rfind("mods/",0)==0||folded.find("update.txt")!=std::string::npos)return false;
    if(relative.find_first_of("<>\"?*")!=std::string::npos)return false;
    std::istringstream components(relative);std::string component;
    while(std::getline(components,component,'/'))if(component.empty()||component.back()=='.'||component.back()==' ')return false;
    return true;
}
}
