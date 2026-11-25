#include "download_animation.hpp"
#include "download_animation_assets.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

int main(){
    using namespace unbound::download_animation;
    constexpr Frame sample[]={{0,0,0,30,40,50},{0,30,0,30,40,130},{0,60,0,30,40,190}};
    const Clip clip{sample,3,190};
    assert(frameIndex(clip,1000,1000)==0);
    assert(frameIndex(clip,1049,1000)==0);
    assert(frameIndex(clip,1050,1000)==1);
    assert(frameIndex(clip,1129,1000)==1);
    assert(frameIndex(clip,1130,1000)==2);
    assert(frameIndex(clip,1190,1000)==0);
    assert(frameIndex(clip,5000,1000)==0); // Skips elapsed frames after a long wait.
    constexpr auto maximum=std::numeric_limits<std::uint32_t>::max();
    assert(frameIndex(clip,49,maximum-50)==1);
    assert(frameIndex(clip,139,maximum-50)==0);
    assert(frameIndex({nullptr,0,0},123,0)==0);
    assert(!redrawDue(1033,1000)&&redrawDue(1034,1000));
    assert(!redrawDue(20,maximum-10)&&redrawDue(23,maximum-10));
    assert(eligible("Downloading Unbound 0.2.0",true));
    assert(!eligible("Downloading Unbound 0.2.0",false));
    assert(!eligible("Installing Unbound",true));
    assert(!eligible("Stopping download safely",true));
    for(int height=246;height<=408;++height){
        const auto arranged=layout(height,true);
        assert(arranged.visible&&arranged.cardTop>=102);
        assert(arranged.cardTop+height+20<=arranged.rowTop);
        assert(arranged.rowTop+128<=658);
    }
    assert(!layout(409,true).visible&&!layout(490,true).visible);
    assert(!layout(246,false).visible&&layout(246,false).cardTop==257);
    std::size_t totalBytes=1280*720*4;
    for(const auto& atlas:assets::Atlases){
        assert(atlas.width>0&&atlas.width<=1024&&atlas.height>0&&atlas.height<=1024);
        totalBytes+=atlas.width*atlas.height*4;
    }
    assert(totalBytes==assets::TotalRgbaBytes&&totalBytes<=12*1024*1024);
    const std::size_t counts[]={297,6,24};
    const std::uint32_t durations[]={14850,480,1440};
    for(std::size_t i=0;i<assets::Clips.size();++i){
        const auto& generated=assets::Clips[i];
        assert(generated.count==counts[i]&&generated.durationMs==durations[i]);
        std::uint32_t previous=0;
        for(std::size_t f=0;f<generated.count;++f){
            const auto& frame=generated.frames[f];
            assert(frame.endMs>previous&&frame.endMs<=generated.durationMs);
            assert(frame.atlas<assets::Atlases.size());
            const auto& sheet=assets::Atlases[frame.atlas];
            assert(frame.x+frame.width<=sheet.width&&frame.y+frame.height<=sheet.height);
            assert(frameIndex(generated,frame.endMs-1,0)==f);
            previous=frame.endMs;
        }
        assert(previous==generated.durationMs&&frameIndex(generated,generated.durationMs,0)==0);
    }
    std::puts("Download animation deadlines, wraparound, layout, frames and memory-budget tests passed");
}
