#pragma once
#include "package_layout.hpp"
#include "map_packs.hpp"
#include <limits>

namespace unbound {
struct LocalPackagePending { LocalPackageInfo package;Game game=Game::BlackOpsII;std::string root,language;std::uint64_t title=0;Device device=Device::Missing; };
inline bool localPackageNumber(const std::string& text,std::uint64_t& value){
    value=0;if(text.empty()||text.size()>20)return false;
    for(char c:text){if(c<'0'||c>'9'||value>(std::numeric_limits<std::uint64_t>::max()-unsigned(c-'0'))/10)return false;value=value*10+unsigned(c-'0');}return true;
}
inline bool parseLocalPackageJournal(const std::string& text,LocalPackagePending& pending,std::string& error){
    pending={};error.clear();std::istringstream lines(text);std::string header,path,hash,size,target,extra;
    std::getline(lines,header);std::getline(lines,path);std::getline(lines,hash);std::getline(lines,size);std::getline(lines,target);
    for(auto* value:{&header,&path,&hash,&size,&target})if(!value->empty()&&value->back()=='\r')value->pop_back();
    const auto fields=packFields(target);std::uint64_t bytes=0,title=0,update=0,sd=0;
    const auto invalid=[&](){error="The unfinished local-package record is invalid. Preserve it and export a Support report.";return false;};
    if(text.size()>4096||header!="UNBOUND_LOCAL_PACKAGE_V1"||!sdBrowsePath(path)||!zipFilename(path)||hash.size()!=64||
       !std::all_of(hash.begin(),hash.end(),[](unsigned char c){return std::isxdigit(c);})||
       !localPackageNumber(size,bytes)||!bytes||bytes>config::MaxArchiveBytes||fields.size()!=7)return invalid();
    if((fields[0]!="BO2"&&fields[0]!="GHOSTS")||!localPackageNumber(fields[1],title)||
       !supportedLanguageFolder(fields[4])||!localPackageNumber(fields[5],update)||!localPackageNumber(fields[6],sd)||
       (!update&&!sd)||update>config::MaxArchiveFiles||sd>config::MaxArchiveFiles||update+sd>config::MaxArchiveFiles)return invalid();
    const auto game=fields[0]=="GHOSTS"?Game::Ghosts:Game::BlackOpsII;
    const auto device=fields[2]=="USB"?Device::USB:fields[2]=="NAND"?Device::MLC:Device::Missing;
    if(update){const auto* sku=findSkuByTitle(title);if(!sku||sku->game!=game||sku->updateTitleId!=title||device==Device::Missing||fields[3].empty())return invalid();}
    while(std::getline(lines,extra))if(!trim(extra).empty())return invalid();
    pending.package={path,hash,bytes,0,0,unsigned(update),unsigned(sd)};
    pending.game=game;pending.title=title;pending.device=device;pending.root=fields[3];pending.language=fields[4];return true;
}
inline std::string localPackageJournal(const LocalPackageInfo& info,const GameInstall& install){
    return "UNBOUND_LOCAL_PACKAGE_V1\n"+info.path+"\n"+info.sha256+"\n"+std::to_string(info.compressedBytes)+"\n"+
        (install.game==Game::Ghosts?"GHOSTS":"BO2")+"|"+std::to_string(install.sku?install.sku->updateTitleId:0)+"|"+
        (install.update.device==Device::USB?"USB":install.update.device==Device::MLC?"NAND":"SD")+"|"+install.update.root+"|"+
        install.languageFolder+"|"+std::to_string(info.updateFiles)+"|"+std::to_string(info.sdFiles)+"\n";
}
inline bool sameLocalPackageTarget(const LocalPackagePending& pending,const GameInstall& install){
    return pending.game==install.game&&pending.language==install.languageFolder&&(!pending.package.updateFiles||
        (install.sku&&install.sku->updateTitleId==pending.title&&install.update.device==pending.device&&install.update.root==pending.root&&install.destinationReady&&install.update.present));
}
inline bool sameLocalPackageSource(const LocalPackagePending& pending,const LocalPackageInfo& info){
    return pending.package.path==info.path&&pending.package.sha256==info.sha256&&pending.package.compressedBytes==info.compressedBytes;
}
}
