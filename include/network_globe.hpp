#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <vector>
namespace unbound {
class NetworkGlobe {
public:
 void draw(SDL_Renderer*,int cx,int cy,int radius,float yaw,float pitch,const std::array<unsigned char,648>& cells,
           SDL_Color heatColor={255,105,18,255});
 void clear();
private:
 struct Sample {float x,y,z;unsigned short longitude,latitude;unsigned char light;};
 SDL_Texture* texture_=nullptr;SDL_Surface* map_=nullptr;
 std::vector<Sample> samples_;std::vector<Uint32> pixels_;
 std::vector<unsigned short> angles_;
 std::array<unsigned short,4097> latitudes_{};
 int size_=0;float pitch_=99;
 bool attempted_=false;
};
}
