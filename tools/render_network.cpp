// Desktop-only layout verification. This compiles the real UI/model sources,
// uses the packaged fonts/artwork, and captures their SDL software output.
// It is not part of the Wii U app or its downloads/install path.
//
// Native build (SDL2, SDL2_image and SDL2_ttf development packages required):
// c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource (continued below)
//   tools/render_install_menu.cpp source/ui.cpp source/model.cpp (continued)
//   $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -o /tmp/render-install
// Run from a temporary directory with fs:/vol/content symlinked to the actual
// content directory, then pass the absolute screenshot output directory.

#include "ui.hpp"
#include "input.hpp"
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace unbound;

GameInstall fixtureInstall(const GameSku* sku, Device updateDevice,
                           Device gameDevice, const std::string& language) {
    GameInstall install;
    install.sku = sku;
    install.consoleRegion = Region::USA;
    install.base = {gameDevice, {}, 0, true};
    install.update.device = updateDevice;
    install.update.present = true;
    install.update.titleVersion = requiredOfficialUpdateVersion(sku);
    install.update.root = updateDevice == Device::USB ? "storage_usb01:/" : "storage_mlc01:/";
    const std::string titleKey = installTargetKey(sku->updateTitleId, updateDevice);
    install.update.root += "usr/title/0005000e/" + titleKey.substr(titleKey.size() - 8);
    install.languageFolder = language;
    install.modVersion = "0.0.05P2.1";
    return install;
}

bool screenshot(const std::filesystem::path& path) {
    SDL_Window* window = SDL_GetWindowFromID(1);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    int width = 0, height = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        std::cerr << "Cannot locate UI renderer: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                         SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        std::cerr << "Cannot allocate capture: " << SDL_GetError() << '\n';
        return false;
    }
    const bool ok = SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                                         surface->pixels, surface->pitch) == 0 &&
                    IMG_SavePNG(surface, path.string().c_str()) == 0;
    if (!ok) std::cerr << "Capture failed: " << SDL_GetError() << ' ' << IMG_GetError() << '\n';
    SDL_FreeSurface(surface);
    if (ok) std::cout << path.string() << '\n';
    return ok;
}
}

int main(int argc,char** argv){
 if(argc!=2)return 2;
 std::filesystem::create_directories(argv[1]);
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
 unbound::Ui ui;std::string error;if(!ui.initialize(error)){std::cerr<<error;return 1;}
 auto active=fixtureInstall(&unbound::SupportedSkus[0],unbound::Device::USB,unbound::Device::Disc,"english");
 active.modVersion="0.2.0";
 unbound::NetworkStats stats;stats.available=true;stats.heatAvailable=true;stats.players=124;stats.servers=18;stats.unique=862;stats.platforms={64,32,20,8};
 for(int i:{200,240,270,300,330,360,400,430,460,500})stats.cells[i]=255;
 ui.setNetwork(stats,-.35f,0);ui.setNavigation(0,false);
 ui.home(active,nullptr,"Beta 3 is here",5,"Unbound is up to date");
 if(!screenshot(std::filesystem::path(argv[1])/"home.png"))return 1;
 ui.touchAction({true,false,0,0});ui.touchAction({true,true,500,530});auto tap=ui.touchAction({true,false,500,530});
 if(!tap||tap->kind!=unbound::TouchActionKind::HomeAction||tap->value!=5)return 4;
 ui.setNavigation(5,false);ui.network();
 if(!screenshot(std::filesystem::path(argv[1])/"network.png"))return 1;
 unsigned begin=SDL_GetTicks();for(int i=0;i<120;++i){ui.setNetwork(stats,-.35f+i*.01f,i<60?0:(i-60)*.01f);ui.network();}
 std::cout<<"Desktop software render, ms/frame: "<<(SDL_GetTicks()-begin)/120.0<<" (not Wii U performance)\n";
 stats.available=false;stats.heatAvailable=false;stats.cells={};ui.setNetwork(stats,0,0);ui.network();
 if(!screenshot(std::filesystem::path(argv[1])/"unavailable.png"))return 1;
 ui.shutdown();return 0;
}
