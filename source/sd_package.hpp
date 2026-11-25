#pragma once
#include <string>
#include <vector>

namespace unbound {
// Only immediate children of this folder; no recursive scan or archive guessing.
inline constexpr const char* SdPackageRoot = "fs:/vol/external01/unbound/updates";
inline constexpr const char* GhostsSdPackageRoot = "fs:/vol/external01/unbound-ghosts/updates";
inline std::vector<std::string> sdPackageCandidates(const std::string& url, const char* root = SdPackageRoot) {
    std::vector<std::string> paths;
    const auto clean=url.substr(0,url.find_first_of("?#"));
    const auto slash=clean.find_last_of('/');
    const auto name=slash==std::string::npos?std::string{}:clean.substr(slash+1);
    bool safe=!name.empty()&&name.size()<=200&&name.ends_with(".zip");
    for(unsigned char c:name) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='.'||c=='-'||c=='_')) safe=false;
    if(safe&&name!="update.zip")paths.push_back(std::string(root)+"/"+name);
    paths.push_back(std::string(root)+"/update.zip");
    return paths;
}
}
