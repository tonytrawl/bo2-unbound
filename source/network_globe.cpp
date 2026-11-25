#include "network_globe.hpp"
#include <SDL2/SDL_image.h>
#include <cmath>
#include <algorithm>
namespace unbound {
void NetworkGlobe::clear(){if(texture_)SDL_DestroyTexture(texture_);if(map_)SDL_FreeSurface(map_);texture_=nullptr;map_=nullptr;}
void NetworkGlobe::draw(SDL_Renderer* renderer,int cx,int cy,int radius,float yaw,float pitch,const std::array<unsigned char,648>& cells,SDL_Color heatColor){
 constexpr float pi=3.14159265f;
 if(!attempted_){attempted_=true;angles_.resize(65536);auto* raw=IMG_Load("fs:/vol/content/images/network-globe.webp");if(raw){map_=SDL_CreateRGBSurfaceWithFormat(0,512,256,32,SDL_PIXELFORMAT_RGBA32);if(map_&&SDL_BlitScaled(raw,nullptr,map_,nullptr)!=0){SDL_FreeSurface(map_);map_=nullptr;}SDL_FreeSurface(raw);}
  for(int y=0;y<256;++y)for(int x=0;x<256;++x)angles_[y*256+x]=static_cast<unsigned short>((std::atan2((x-127.5f)/127.5f,(y-127.5f)/127.5f)/(2*pi)+.5f)*65535);
  for(int i=0;i<=4096;++i)latitudes_[i]=static_cast<unsigned short>((.5f-std::asin((i-2048)/2048.0f)/pi)*65535);
 }
 const int wanted=radius<70?96:256;
 if(size_!=wanted){size_=wanted;if(texture_)SDL_DestroyTexture(texture_);texture_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,size_,size_);if(texture_)SDL_SetTextureBlendMode(texture_,SDL_BLENDMODE_BLEND);
  pixels_.resize(size_*size_);samples_.resize(size_*size_);pitch_=99;
  for(int y=0;y<size_;++y)for(int x=0;x<size_;++x){auto& s=samples_[y*size_+x];s.x=(x+.5f-size_/2)/(size_/2);s.y=(size_/2-y-.5f)/(size_/2);const float r=s.x*s.x+s.y*s.y;s.z=r<1?std::sqrt(1-r):-1;s.light=static_cast<unsigned char>(std::clamp((.45f+.65f*std::max(0.0f,(-s.x*.4f+s.y*.5f+s.z)/1.187f))*1.55f,0.0f,1.8f)*140);}
 }
 if(!texture_)return;
 const float cp=std::cos(pitch),sp=std::sin(pitch);
 if(pitch_!=pitch){pitch_=pitch;for(auto& s:samples_)if(s.z>=0){const float y=s.y*cp+s.z*sp,z=s.z*cp-s.y*sp;s.latitude=latitudes_[std::clamp(int(y*2048+2048),0,4096)];s.longitude=angles_[std::clamp(int(z*127.5f+127.5f),0,255)*256+std::clamp(int(s.x*127.5f+127.5f),0,255)];}}
 const int offset=int(yaw/(2*pi)*65536);
 for(size_t i=0;i<samples_.size();++i){const auto& s=samples_[i];if(s.z<0){pixels_[i]=0;continue;}Uint8 r=25,g=37,b=42;
  if(map_){const int x=((s.longitude+offset)&65535)*map_->w/65536,y=s.latitude*(map_->h-1)/65535;const auto* pixel=static_cast<Uint8*>(map_->pixels)+y*map_->pitch+x*4;r=pixel[0];g=pixel[1];b=pixel[2];}
  r=Uint8(std::min(255,int(r)*s.light/140+3));g=Uint8(std::min(255,int(g)*s.light/140+5));b=Uint8(std::min(255,int(b)*s.light/140+6));
  // RGBA32 is byte-order independent, including Wii U's big-endian CPU.
  auto* p=reinterpret_cast<Uint8*>(&pixels_[i]);p[0]=r;p[1]=g;p[2]=b;p[3]=255;
 }
 SDL_UpdateTexture(texture_,nullptr,pixels_.data(),size_*4);const SDL_Rect dst{cx-radius,cy-radius,radius*2,radius*2};SDL_RenderCopy(renderer,texture_,nullptr,&dst);
 const float sy=std::sin(yaw),cyaw=std::cos(yaw);
 SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
 for(int i=0;i<648;++i){if(!cells[i])continue;float lat=(-85+(i/36)*10)*pi/180,lon=(-175+(i%36)*10)*pi/180;
  float x=std::cos(lat)*std::sin(lon),y=std::sin(lat),z=std::cos(lat)*std::cos(lon);
  const float rx=x*cyaw-z*sy,rz=x*sy+z*cyaw,ry=y*cp-rz*sp,front=y*sp+rz*cp;if(front<=.08f)continue;
  const int px=cx+int(radius*rx),py=cy-int(radius*ry),r=radius<70?1:2;
  // Home dots are only 2 pixels wide; no oversized glow tiles.
  SDL_SetRenderDrawColor(renderer,heatColor.r,heatColor.g,heatColor.b,static_cast<Uint8>(std::min(1.0f,front*4)*210));SDL_Rect dot{px-r,py-r,2*r,2*r};SDL_RenderFillRect(renderer,&dot);
 }
}
}
