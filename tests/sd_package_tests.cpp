#include "sd_package.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace unbound;
    auto p=sdPackageCandidates("https://host/releases/unbound-0.2.0.zip?token=x");
    assert(p.size()==2&&p[0]==std::string(SdPackageRoot)+"/unbound-0.2.0.zip");
    auto ghosts=sdPackageCandidates("https://host/ghosts-core.zip",GhostsSdPackageRoot);
    assert(ghosts.size()==2);
    assert(ghosts[0]=="fs:/vol/external01/unbound-ghosts/updates/ghosts-core.zip");
    assert(ghosts[1]=="fs:/vol/external01/unbound-ghosts/updates/update.zip");
    for(const auto* url:{"https://host/a%2fb.zip","https://host/a\\b.zip","https://host/no.zip.exe","https://host/update.zip"}){
        p=sdPackageCandidates(url);assert(p.size()==1&&p[0]==std::string(SdPackageRoot)+"/update.zip");
    }
    std::cout<<"SD package bounded filename and fallback policy passed\n";
}
