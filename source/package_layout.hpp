#pragma once
#include "model.hpp"
#include "movie_install_path.hpp"
#include "install_commit.hpp"
#include "install_discovery.hpp"
#include <sstream>

namespace unbound {
enum class PackageArea { Invalid, Update, Sd };
struct PackageRoute { PackageArea area=PackageArea::Invalid;std::string logical,relative; };
inline PackageRoute packageRoute(std::string path,const std::string& language){
    std::replace(path.begin(),path.end(),'\\','/');
    if(!safeRelativePath(path)||path.find_first_of("<>\"?*|")!=std::string::npos)return {};
    std::istringstream parts(path);std::string part;
    while(std::getline(parts,part,'/'))if(part.empty()||part.back()=='.'||part.back()==' ')return {};
    std::string localized;if(!localizeArchivePath(path,language,localized))return {};
    if(reservedInstallPayloadPath(localized))return {};
    if(localized=="sd")return {PackageArea::Sd,"sd",{}};
    if(localized.rfind("sd/",0)==0){
        const auto relative=localized.substr(3);
        std::string lower=relative;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(lower=="update.txt"||lower.rfind("update.txt/",0)==0)return {};
        return {PackageArea::Sd,localized,relative};
    }
    if(localized=="update"||localized=="update/code"||localized=="update/content"||
       localized.rfind("update/code/",0)==0||localized.rfind("update/content/",0)==0)
        return {PackageArea::Update,localized,localized=="update"?"":localized.substr(7)};
    return {};
}
inline std::string packageSdRoot(Game game){
    return game==Game::Ghosts?"fs:/vol/external01/unbound-ghosts/content":"fs:/vol/external01/unbound/content";
}
inline PackageRoute modPackageRoute(std::string name,const std::string& language){
    if(name=="content"||name.rfind("content/",0)==0)name="update/"+name;
    return packageRoute(name,language);
}
inline std::string modPackageOwnershipKey(const PackageRoute& route){
    if(route.area==PackageArea::Sd)return "sd:"+route.relative;
    if(route.area!=PackageArea::Update)return {};
    if(route.relative=="code")return "code:";
    if(route.relative.rfind("code/",0)==0)return "code:"+route.relative.substr(5);
    return route.relative.size()>8?route.relative.substr(8):std::string{};
}
inline std::string packageDestination(const PackageRoute& route,const GameInstall& install,Game game){
    if(install.game!=game)return {};
    if(route.area==PackageArea::Sd)return packageSdRoot(game)+(route.relative.empty()?"":"/"+route.relative);
    if(route.area!=PackageArea::Update||!install.sku||install.sku->game!=game||!install.update.present||!install.destinationReady||
       (install.update.device!=Device::USB&&install.update.device!=Device::MLC)||install.update.root!=stockTitleRoot(install.update.device,install.sku->updateTitleId))return {};
    if(game==Game::Ghosts)return install.update.root+route.logical.substr(6);
    const auto movie=routeMovieInstallPath(route.logical,install.languageFolder);
    if(movie.status==MovieInstallPathStatus::Invalid)return {};
    const auto slash=movie.destination.find('/');return slash==std::string::npos?install.update.root:install.update.root+"/"+movie.destination.substr(slash+1);
}
inline constexpr const char* SdBrowseRoot="fs:/vol/external01";
inline bool sdBrowsePath(const std::string& path){
    const std::string root=SdBrowseRoot;
    if(path==root)return true;
    if(path.rfind(root+"/",0)!=0||path.size()>600)return false;
    const auto relative=path.substr(root.size()+1);
    if(!safeRelativePath(relative)||relative.find_first_of("<>\"?*|\r\n")!=std::string::npos)return false;
    std::istringstream parts(relative);std::string part;
    while(std::getline(parts,part,'/'))if(part.empty()||part.back()=='.'||part.back()==' ')return false;
    return !relative.empty()&&relative.back()!='/';
}
inline bool zipFilename(std::string name){
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    return name.size()>4&&name.ends_with(".zip");
}
struct SdPackageEntry { std::string name,path;bool directory=false;std::uint64_t bytes=0; };
struct LocalPackageInfo {
    std::string path,sha256;
    std::uint64_t compressedBytes=0,updateBytes=0,sdBytes=0;
    unsigned updateFiles=0,sdFiles=0;
};
}
