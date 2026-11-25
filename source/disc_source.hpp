#pragma once
#include "install_discovery.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace unbound {
struct DiscSourceCandidate { unsigned volume; std::string root; };

// The OS already mounts an inserted game disc; HQ only attaches a devoptab to
// that existing mount. Hardware MCP reports the game at /vol/storage_odd03
// (r41 disc-user report). The content-named mounts remain accepted fallbacks.
struct DiscSourceVolume { const char* root; const char* alias; };
inline constexpr DiscSourceVolume DiscSourceVolumes[]{
    {"/vol/storage_odd03", "hq_disc03"},
    {"/vol/storage_odd04", "hq_disc04"},
    {"/vol/storage_odd_content", "hq_odd03"},
    {"/vol/storage_odd_content2", "hq_odd04"},
};
inline constexpr unsigned DiscSourceVolumeCount = sizeof(DiscSourceVolumes) / sizeof(DiscSourceVolumes[0]);

// Only OS disc volumes are permitted. MCP supplies the title subpath;
// never treat an arbitrary MCP/SD/UPDATE path as an original-game source.
inline std::vector<DiscSourceCandidate> discSourceCandidates(const std::string& mcpPath,
                                                           std::uint64_t titleId) {
    std::vector<DiscSourceCandidate> result;
    for (unsigned i=0;i<DiscSourceVolumeCount;++i) {
        const std::string volume=DiscSourceVolumes[i].root;
        const std::string alias=std::string(DiscSourceVolumes[i].alias)+":";
        if (!mcpPath.empty() && mcpPath!=volume && mcpPath.rfind(volume+"/",0)!=0) continue;
        const auto suffix=mcpPath.empty()?std::string{}:mcpPath.substr(volume.size());
        if (suffix.find('\\')!=std::string::npos || suffix.find(':')!=std::string::npos) return {};
        std::size_t begin=suffix.empty()?0:1;
        while(begin<suffix.size()) {
            const auto end=suffix.find('/',begin);
            const auto part=suffix.substr(begin,end==std::string::npos?end:end-begin);
            if(part.empty()||part=="."||part=="..")return {};
            for(unsigned char ch:part)if(ch<32||ch==127)return {};
            if(end==std::string::npos)break;
            begin=end+1;
        }
        if(suffix.size()>400)return {};
        if(!suffix.empty())result.push_back({i,alias+suffix});
        char title[64];
        std::snprintf(title,sizeof(title),"/usr/title/%08x/%08x",
            static_cast<unsigned>(titleId>>32),static_cast<unsigned>(titleId));
        const auto canonical=alias+title;
        if(result.empty()||result.back().root!=canonical)result.push_back({i,canonical});
        result.push_back({i,alias+"/"});
    }
    return result;
}

template<class Io>
bool resolveDiscSource(const std::string& mcpPath,std::uint64_t expectedTitle,
                       std::string& content,std::uint16_t& version,Io& io) {
    content.clear();version=0;
    for(const auto& candidate:discSourceCandidates(mcpPath,expectedTitle)) {
        if(!io.attach(candidate.volume))continue;
        const auto root=candidate.root.ends_with('/')?candidate.root.substr(0,candidate.root.size()-1):candidate.root;
        std::string metadata;std::uint16_t found=0;
        if(!io.read(root+"/code/app.xml",metadata)||!stockTitleMetadata(metadata,expectedTitle,found))continue;
        if(!io.directory(root+"/content"))continue;
        content=root+"/content";version=found;return true;
    }
    return false;
}
} // namespace unbound
