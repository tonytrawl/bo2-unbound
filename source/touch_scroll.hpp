#pragma once
#include "touch.hpp"
#include <algorithm>
#include <cmath>

namespace unbound {
// Allocation-free, time-based scrolling in the UI's calibrated 1280x720 space.
class TouchScroll {
public:
    void reset(){blocked_=true;pressed_=dragging_=false;velocity_=0;}
    bool update(TouchSample sample,std::uint32_t now,int maximum,int& offset,TouchRect viewport={244,102,1008,556}){
        maximum=std::max(0,maximum);
        if(!sample.valid){reset();return false;}
        if(blocked_){if(!sample.touching)blocked_=false;lastTick_=now;return false;}
        const float dt=std::min<std::uint32_t>(now-lastTick_,40)/1000.0f;lastTick_=now;
        if(sample.touching){
            if(!pressed_){
                if(!viewport.contains(sample.x,sample.y)){reset();return false;}
                pressed_=true;dragging_=false;velocity_=0;position_=offset;
                startY_=lastY_=sample.y;lastMotion_=now;return false;
            }
            if(!dragging_&&std::abs(sample.y-startY_)>=8)dragging_=true;
            if(dragging_){
                const int delta=lastY_-sample.y;
                if(delta){
                    position_=std::clamp(position_+delta,0.0f,float(maximum));
                    if(dt>0)velocity_=std::clamp(velocity_*0.35f+(delta/dt)*0.65f,-1800.0f,1800.0f);
                    lastMotion_=now;
                }else if(now-lastMotion_>80)velocity_=0;
                offset=static_cast<int>(std::lround(position_));
            }
            lastY_=sample.y;return dragging_;
        }
        if(pressed_){pressed_=false;if(!dragging_)velocity_=0;if(now-lastMotion_>80)velocity_=0;}
        if(std::abs(velocity_)<12){velocity_=0;dragging_=false;return false;}
        position_=std::clamp(position_+velocity_*dt,0.0f,float(maximum));
        velocity_*=std::exp(-7.0f*dt);
        if(position_<=0||position_>=maximum)velocity_=0;
        offset=static_cast<int>(std::lround(position_));return true;
    }
private:
    bool blocked_=true,pressed_=false,dragging_=false;
    int startY_=0,lastY_=0;
    float velocity_=0,position_=0;
    std::uint32_t lastTick_=0,lastMotion_=0;
};
}
