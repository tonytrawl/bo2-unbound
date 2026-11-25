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
#include "config.hpp"
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
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
 const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
 Ui ui;std::string error;if(!ui.initialize(error)){std::cerr<<error;return 1;}
 const std::string hash(64,'a');
 const std::string bo2Text="UNIVERSAL|FULL|-|1.0.4|https://example.test/core.zip|"+hash+"|441984130\n"
     "PACK|pack-1|10|Unbound DLC 1: Revolution|Revolution maps\n"
     "FILE|pack-1|FULL|-|1.0.1|https://example.test/dlc.zip|"+hash+"|1363428316\n"
     "FILE|pack-1|PATCH|1.0.0|1.0.1|https://example.test/patch.zip|"+hash+"|150780195\n";
 const std::string ghostsText="UNIVERSAL|FULL|-|1.0.1|https://example.test/ghosts.zip|"+hash+"|7068202\n";

 const auto bo2=parseReleases(bo2Text),ghosts=parseReleases(ghostsText);
 std::vector<MapPack> packs;if(!parseMapPacks(bo2Text,packs,error)||packs.empty())return 3;
 config::activeGame=Game::BlackOpsII;
 auto install=fixtureInstall(&SupportedSkus[0],Device::USB,Device::Disc,"english");install.modVersion="1.0.3";
 const auto core=releasePlanForSku(bo2,install.sku,install.modVersion);
 if(core.empty())return 4;
 std::uint64_t coreBytes=0;for(const auto* row:core)coreBytes+=row->compressedBytes;
 const auto* pack=mapPackRelease(packs[0],"1.0.0");if(!pack)return 5;
 auto combined=quickUpdatePlan(bo2,install,{{&packs[0],"1.0.0"}},"1.0.0");
 const auto estimates=[](auto& rows){for(auto& row:rows){row.networkBytes=row.bytes;row.downloadSeconds=UpdateTimingHistory::seconds(row.bytes,2*1024*1024,1);row.installSeconds=UpdateTimingHistory::seconds(row.bytes,1024*1024,1);row.timingFromHistory=true;}};
 estimates(combined.rows);
 ui.setNavigation(1,false);ui.quickUpdate(install,combined.rows,combined.coreCurrent);
 if(!screenshot(output/"quick-update-core-and-dlc.png"))return 6;
 ui.touchAction({true,false,0,0});ui.touchAction({true,true,1060,565});auto action=ui.touchAction({true,false,1060,565});
 if(!action||action->kind!=TouchActionKind::Button||action->value!=int(button::A))return 7;
 ui.touchAction({true,true,1050,630});action=ui.touchAction({true,false,1050,630});
 if(action)return 8;
 ui.touchAction({true,true,240,590});action=ui.touchAction({true,false,240,590});if(!action||action->kind!=TouchActionKind::Button||action->value!=int(button::B))return 14;
 install.modVersion="1.0.4";auto mapOnly=quickUpdatePlan(bo2,install,{{&packs[0],"1.0.0"}},"1.0.0");estimates(mapOnly.rows);ui.quickUpdate(install,mapOnly.rows,mapOnly.coreCurrent);
 if(!screenshot(output/"quick-update-map-pack-only.png"))return 9;
 config::activeGame=Game::Ghosts;
 auto ghostInstall=fixtureInstall(&GhostsSkus[0],Device::USB,Device::Disc,"english");ghostInstall.game=Game::Ghosts;ghostInstall.modVersion="1.0.0";
 const auto ghostPlan=releasePlanForSku(ghosts,ghostInstall.sku,ghostInstall.modVersion);
 if(ghostPlan.size()!=1)return 10;
 auto ghostUpdate=quickUpdatePlan(ghosts,ghostInstall,{},"");estimates(ghostUpdate.rows);ui.quickUpdate(ghostInstall,ghostUpdate.rows,ghostUpdate.coreCurrent);
 if(!screenshot(output/"quick-update-ghosts.png"))return 11;
 config::activeGame=Game::BlackOpsII;
 std::vector<QuickUpdateRow> longList(33,mapOnly.rows.front());
 for(std::size_t i=0;i<longList.size();++i)longList[i].title="Map pack "+std::to_string(i+1);
 ui.quickUpdate(install,longList,true,quickUpdateMaximumScroll(longList.size()));
 if(!screenshot(output/"quick-update-long-list.png"))return 12;
 ui.touchAction({true,false,0,0});ui.touchAction({true,true,1060,565});action=ui.touchAction({true,false,1060,565});
 if(!action||action->kind!=TouchActionKind::Button||action->value!=int(button::A))return 13;
 ui.shutdown();std::cout<<"Production quick-update layouts and controls passed for Core/maps, map-only, Ghosts and 33-row scrolling\n";return 0;
}
