#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace unbound::download_animation {

struct Atlas {
    const char* path;
    int width;
    int height;
};

struct Frame {
    std::uint16_t atlas;
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t endMs;
};

struct Clip {
    const Frame* frames;
    std::size_t count;
    std::uint32_t durationMs;
};

// Original frame deadlines, rather than download callbacks, determine the
// displayed frame. Unsigned subtraction also handles SDL's tick wraparound.
inline std::size_t frameIndex(const Clip& clip,std::uint32_t now,
                              std::uint32_t started) {
    if(!clip.count||!clip.durationMs)return 0;
    const std::uint32_t phase=(now-started)%clip.durationMs;
    std::size_t first=0,last=clip.count;
    while(first<last){
        const std::size_t middle=first+(last-first)/2;
        if(phase<clip.frames[middle].endMs)last=middle;
        else first=middle+1;
    }
    return first<clip.count?first:clip.count-1;
}

struct Layout {
    int cardTop;
    int rowTop;
    bool visible;
};

inline bool eligible(std::string_view title,bool cancellable) {
    return cancellable&&title.starts_with("Downloading ");
}

inline Layout layout(int cardHeight,bool enabled) {
    constexpr int top=102,bottom=658,rowHeight=128,gap=20;
    const int available=bottom-top;
    if(!enabled||cardHeight+rowHeight+gap>available)
        return {top+(available-cardHeight)/2,0,false};
    const int cardTop=top+(available-cardHeight-rowHeight-gap)/2;
    const int rowTop=cardTop+cardHeight+gap;
    return {cardTop,rowTop<500?500:rowTop,true};
}

inline bool redrawDue(std::uint32_t now,std::uint32_t previous) {
    return now-previous>=34; // At most 30 redraws/s, including across tick wrap.
}

} // namespace unbound::download_animation
