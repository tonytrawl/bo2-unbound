#pragma once
#include "touch.hpp"
#include <algorithm>
#include <cmath>
namespace unbound {
class GlobeMotion {
public:
 float yaw=-0.35f,pitch=0;
 void resetGesture(){blocked_=true;pressed_=false;vx_=vy_=0;last_=0;}
 bool update(const TouchSample& touch,unsigned now,bool expanded){
  const float dt=last_?std::min(now-last_,40u)/1000.0f:0;last_=now;
  if(!expanded||!touch.valid){pressed_=false;blocked_=true;}
  else if(!touch.touching){blocked_=false;pressed_=false;}
  else if(!blocked_){
   const int dx=touch.x-554,dy=touch.y-477;
   if(!pressed_){if(dx*dx+dy*dy<=137*137){pressed_=true;x_=touch.x;y_=touch.y;vx_=vy_=0;}}
   else{
    const int dx2=touch.x-x_,dy2=touch.y-y_;x_=touch.x;y_=touch.y;
    yaw-=dx2*0.009f;pitch=std::clamp(pitch+dy2*0.009f,-1.1f,1.1f);
    if(dt>0){vx_=0.4f*vx_+0.6f*std::clamp(-dx2*0.009f/dt,-5.0f,5.0f);vy_=0.4f*vy_+0.6f*std::clamp(dy2*0.009f/dt,-3.0f,3.0f);}
   }
  }
  if(!pressed_){const float coast=std::exp(-2.8f*dt);vx_*=coast;vy_*=coast;
   // Ease the automatic rotation back in as the user's momentum settles.
   const float autoBlend=1.0f-std::min(1.0f,std::abs(vx_)/0.12f+std::abs(vy_)/0.12f);
   yaw+=(vx_+0.055f*autoBlend)*dt;pitch=std::clamp(pitch+vy_*dt,-1.1f,1.1f);
  }
  yaw=std::remainder(yaw,6.2831853f);return pressed_;
 }
private: bool blocked_=true,pressed_=false;int x_=0,y_=0;unsigned last_=0;float vx_=0,vy_=0;
};
}
