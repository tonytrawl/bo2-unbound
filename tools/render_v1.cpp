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
 const std::filesystem::path output=argv[1];
 std::filesystem::create_directories(output);
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
 Ui ui;std::string error;if(!ui.initialize(error)){std::cerr<<error;return 1;}
 const auto capture=[&](const std::string& name){return screenshot(output/(name+".png"));};
 const auto tap=[&](int x,int y,TouchActionKind kind,int value){
     ui.touchAction({true,false,0,0});ui.touchAction({true,true,x,y});
     const auto action=ui.touchAction({true,false,x,y});
     return action&&action->kind==kind&&action->value==value;
 };
 auto active=fixtureInstall(&SupportedSkus[0],Device::USB,Device::Disc,"english");
 active.modVersion="1.0.0";
 Release latest{"UNIVERSAL",ReleaseKind::Full,"-","1.0.2","","",440401920};
 NetworkStats stats;stats.available=true;stats.players=187;stats.servers=24;stats.unique=4000;
 ui.setNetwork(stats,0,0);
 for(const auto game:{Game::BlackOpsII,Game::Ghosts}){
     config::activeGame=game;
     auto install=fixtureInstall(&gameSkus(game).front(),Device::USB,Device::Disc,"english");
     install.modVersion="1.0.0";
     ui.setNavigation(0,false);
     ui.home(install,&latest,"# Unbound V1.0 + Ghosts UNC\n\nMore maps, new modes, and a new way to play online.\n",4,"",false,false);
     if(!capture(game==Game::Ghosts?"home-ghosts":"home-bo2"))return 3;
     const bool ghosts=game==Game::Ghosts;
     if(!tap(ghosts?350:560,51,TouchActionKind::GameChoice,ghosts?0:1))return 4;
 }
 config::activeGame=Game::BlackOpsII;
 ui.setNavigation(1,false);
 ui.installReview(active,"1.0.2","",3,true,false,false,true,true);
 if(!capture("install-review")||!tap(1050,570,TouchActionKind::InstallRow,3)||!tap(400,620,TouchActionKind::InstallRow,5))return 5;
 ui.installMenu(active,{{active,{"english","french"}}},{},0,"1.0.2","",true,true,true);
 if(!capture("install-settings")||!tap(814,300,TouchActionKind::InstallChoice,1))return 6;
 ui.installReview(active,"1.0.2","Update ZIP found on your SD card.",3,true,true,false,true);
 if(!capture("install-from-sd"))return 7;
 const MapPack pack{"pack-1","Unbound DLC Pack 1","Adds Downhill, Hydro, Mirage, and Grind for Multiplayer, plus Die Rise and Nuketown Zombies.",10,
     {"UNIVERSAL",ReleaseKind::Full,"-","1.0.0","","",1366100485},{}};
 std::vector<MapPack> packs{pack};
 ui.mapPacks(packs,{true},{""},0,active,"Unchecked packs keep their installed files.");
 if(!capture("map-packs")||!tap(400,400,TouchActionKind::MapPackRow,0)||
    !tap(1000,555,TouchActionKind::MapPackDetails,0)||
    !tap(1050,620,TouchActionKind::MapPackRow,1)||
    !tap(450,620,TouchActionKind::MapPackRow,2))return 8;
 packs.resize(32,pack);std::vector<bool> chosen(32,false);chosen[31]=true;
 std::vector<std::string> versions(32,"1.0.0");
 for(int i=0;i<32;++i){packs[i].name="DLC Pack "+std::to_string(i+1);packs[i].id=std::to_string(i);}
 ui.mapPacks(packs,chosen,versions,31,active,"Selection is saved for next time.");
 if(!capture("map-packs-scrolled")||!tap(400,520,TouchActionKind::MapPackRow,31))return 9;
 auto nand=active;nand.update.device=Device::MLC;
 ui.mapPacks({pack},{true},{"1.0.0"},1,nand,"Install destination: internal storage.");
 if(!capture("map-packs-nand"))return 10;
 ui.mapPacks({pack},{true},{"1.0.0"},2,active,"Core is left unchanged. Reinstall the selected map packs.",true);
 if(!capture("map-packs-reinstall")||!tap(450,620,TouchActionKind::MapPackRow,2))return 25;
 ui.mapPacks({}, {}, {},0,active,"Map catalog offline. Core is still available.");
 if(!capture("map-packs-offline"))return 11;
 ui.contentQueue({{"Unbound Core 1.0.2","420 MiB / USB storage"},{"Unbound DLC Pack 1 1.0.0","1.27 GiB / USB storage"}},active,"1.68 GiB",0);
 if(!capture("content-queue")||!tap(1050,615,TouchActionKind::Button,int(button::A)))return 12;
 std::vector<WorkshopPlugin> plugins{
     {"UNBOUND_MOD_LINK","Mod Load Plugin","Unbound","Loads your installed Unbound mods and keeps required files ready for the game.","","",{"wiiu/environments/aroma/plugins/modload.wps"}},
     {"GAMEPAD_MIC_REDIRECT","GamePad Plugin","Community","Use the Wii U GamePad microphone in supported games.","","",{}},
     {"PROTARIUM","Protarium Plugin","Protarium Network","An alternative network for the original online experience.","","",{}}
 };
 PluginInstallStatus installed;installed.presence=PluginPresence::Managed;installed.installedVersion="1.0.0";installed.installedRevision=1;
 ui.setNavigation(2,false);
 ui.pluginCatalog(plugins,{installed,{},{}},{true,true,true},0,0,"",{true,true,true});
 if(!capture("workshop")||!tap(1050,580,TouchActionKind::PluginRow,0))return 13;
 if(!tap(767,146,TouchActionKind::WorkshopTab,1))return 50;
 ui.touchAction({true,true,1050,580});
 ui.workshopContent(WorkshopTab::CustomMaps);
 if(ui.touchAction({true,false,1050,580}))return 51;
 if(!capture("workshop-custom-maps")||!tap(573,146,TouchActionKind::WorkshopTab,0))return 52;
 ui.touchAction({true,true,1050,580});
 if(ui.touchAction({true,false,1050,580}))return 53;
 if(!tap(105,300,TouchActionKind::Sidebar,2))return 54;
 if(!tap(1060,690,TouchActionKind::Button,int(button::B)))return 55;
 config::activeGame=Game::Ghosts;
 ui.workshopContent(WorkshopTab::CustomMaps);
 if(!capture("workshop-custom-maps-ghosts")||!tap(767,146,TouchActionKind::WorkshopTab,1))return 56;
 config::activeGame=Game::BlackOpsII;
 ui.pluginCatalog(plugins,{installed,{},{}},{true,true,true},1,0,"",{true,true,true});
 if(!tap(1050,580,TouchActionKind::PluginRow,1)||!tap(767,146,TouchActionKind::WorkshopTab,1))return 57;
 // Both arrow buttons remain touchable in the footer.
 if(!tap(565,690,TouchActionKind::Button,int(button::Left))||
    !tap(615,690,TouchActionKind::Button,int(button::Right)))return 58;
 MapPack custom;custom.id="custom-island";custom.name="Island Outpost";
 custom.description="A community survival map. Hold the outpost and watch the shoreline.";
 custom.full={"UNIVERSAL",ReleaseKind::Full,"","1.0.1","https://example.test/Island-v1.0.1.zip",std::string(64,'a'),268435456};
 custom.patches.push_back({"UNIVERSAL",ReleaseKind::Patch,"1.0.0","1.0.1","https://example.test/Island-patch.zip",std::string(64,'b'),10485760});
 ui.workshopContent(WorkshopTab::CustomMaps,{custom},{"1.0.0"},0,0,"",true);
 if(!capture("workshop-custom-maps-update")||!tap(1050,580,TouchActionKind::CustomMapAction,0)||
    !tap(450,300,TouchActionKind::CustomMapRow,0))return 59;
 ui.workshopContent(WorkshopTab::CustomMaps,{custom},{"1.0.1"},0,0,"",true);
 if(!capture("workshop-custom-maps-reinstall")||!tap(1050,580,TouchActionKind::CustomMapAction,0))return 60;
 ui.workshopContent(WorkshopTab::CustomMaps,{custom},{"1.0.2"},0,0,"",true);
 ui.touchAction({true,true,1050,580});if(ui.touchAction({true,false,1050,580}))return 61;
 ui.workshopContent(WorkshopTab::Mods);
 if(!capture("workshop-mods-empty")||!tap(961,146,TouchActionKind::WorkshopTab,2))return 70;
 MapPack mod=custom;mod.id="mod-survival";mod.name="Survival Rules";
 mod.description="A sample gameplay mod. Install it, then enable it from the game's mod menu.";
 mod.full.compressedBytes=4194304;
 ui.workshopContent(WorkshopTab::Mods,{mod},{""},0,0,"",true);
 if(!capture("workshop-mods-install")||!tap(1050,580,TouchActionKind::CustomMapAction,0)||!tap(450,300,TouchActionKind::CustomMapRow,0))return 71;
 ui.workshopContent(WorkshopTab::Mods,{mod},{"1.0.0"},0,0,"",true);
 if(!capture("workshop-mods-update")||!tap(1050,580,TouchActionKind::CustomMapAction,0))return 72;
 ui.workshopContent(WorkshopTab::Mods,{mod},{"1.0.1"},0,0,"",true);
 if(!capture("workshop-mods-reinstall"))return 73;
 ui.workshopContent(WorkshopTab::Mods,{mod},{"1.0.2"},0,0,"",true);
 ui.touchAction({true,true,1050,580});if(ui.touchAction({true,false,1050,580}))return 74;
 config::activeGame=Game::Ghosts;
 ui.workshopContent(WorkshopTab::Mods,{mod},{""},0,0,"",true);
 if(!capture("workshop-mods-ghosts")||!tap(1155,146,TouchActionKind::WorkshopTab,3))return 75;
 config::activeGame=Game::BlackOpsII;
 std::vector<SdPackageEntry> packages{{"..","fs:/vol/external01/unbound",true,0},{"Community maps","fs:/vol/external01/unbound/packages/Community maps",true,0},{"Local-Release.zip","fs:/vol/external01/unbound/packages/Local-Release.zip",false,435552442}};
 ui.localPackages("fs:/vol/external01/unbound/packages",packages,2,0,"");
 if(!capture("workshop-custom-packages")||!tap(1060,580,TouchActionKind::PackageEntry,2)||!tap(440,325,TouchActionKind::PackageEntry,1))return 65;
 if(!tap(573,146,TouchActionKind::WorkshopTab,0)||!tap(767,146,TouchActionKind::WorkshopTab,1)||!tap(1155,146,TouchActionKind::WorkshopTab,3))return 66;
 if(!tap(640,690,TouchActionKind::Button,int(button::Y)))return 67;
 ui.localPackages("fs:/vol/external01",{},0,0,"No folders or ZIP packages here.");
 if(!capture("workshop-custom-packages-empty"))return 68;
 PluginRelease pluginRelease;pluginRelease.version="1.0.1";pluginRelease.revision=2;
 ui.pluginDetails(plugins[0],&pluginRelease,installed,true,0,0,"","Release check complete.");
 if(!capture("plugin-details"))return 14;
 ui.setNavigation(1,false);
 ui.downloadLoading("Downloading Unbound Core","176 MiB of 420 MiB / 2.1 MiB/s",42,true);
 if(!ui.waitingGameActive()||!capture("download-fullscreen"))return 15;
 if(!ui.operationScreenActive()||!tap(800,690,TouchActionKind::Button,int(button::X)))return 34;
 ui.waitingGameInput(button::X);SDL_Delay(320);ui.animateDownloadWaiting();
 if(ui.waitingGameActive()||!ui.operationScreenActive()||ui.supplyRunEnabled())return 35;
 if(!tap(960,690,TouchActionKind::Button,int(button::B)))return 36;
 ui.waitingGameInput(0,button::X);
 if(ui.supplyRunEnabled())return 37;
 if(!tap(320,690,TouchActionKind::Button,int(button::X)))return 38;
 ui.waitingGameInput(button::X);SDL_Delay(320);ui.animateDownloadWaiting();
 if(!ui.waitingGameActive()||!ui.supplyRunEnabled())return 39;

#ifdef HQ_LAYOUT_PREVIEW
 ui.waitingGameInput(0,0,{true,true,480,690});
 ui.waitingGamePreviewFrame(SDL_GetTicks()+33);
 if(!ui.waitingGamePreviewState().ducking()||!capture("supply-run-touch-duck"))return 32;
 ui.waitingGameInput(0,0,{true,false,480,690});
 if(ui.waitingGamePreviewState().ducking())return 33;
#endif
 if(!tap(500,490,TouchActionKind::Button,int(button::A)))return 16;
 ui.waitingGameInput(button::A);
 SDL_Delay(100);ui.animateDownloadWaiting();
 if(!capture("supply-run-jump"))return 17;
#ifdef HQ_LAYOUT_PREVIEW
 std::array<bool,SupplyRun::Obstacles.size()> captured{};
 bool duckGull=false,duckPlane=false,lisaPickup=false,lisaShield=false,lisaDance=false;
 int lisaFirstPhase=-1;
 std::uint32_t virtualTime=SDL_GetTicks();
 for(unsigned frame=0;frame<7200;++frame){
     const auto& game=ui.waitingGamePreviewState();
     const SupplyRun::Obstacle* incoming=nullptr;
     for(const auto& obstacle:game.obstacles()){
         if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
         if(!incoming||obstacle.x<incoming->x)incoming=&obstacle;
     }
     const bool duck=incoming&&SupplyRun::overhead(incoming->kind)&&incoming->x<265;
     ui.waitingGameInput(0,duck?button::Down:0);
     if(incoming&&!SupplyRun::overhead(incoming->kind)&&incoming->x>SupplyRun::PlayerX&&incoming->x<240&&game.height()==0)ui.waitingGameInput(button::A);
     ui.waitingGamePreviewFrame(virtualTime+=33);
     if(game.crashed())return 22;
     if(game.powerupVisible()&&game.powerupX()>300&&game.powerupX()<600&&!lisaPickup){lisaPickup=true;if(!capture("supply-run-lisa-pickup"))return 62;}
     if(game.invincible()&&!lisaShield){lisaShield=true;lisaFirstPhase=int((virtualTime/170)%2);if(!capture("supply-run-lisa-shield"))return 63;}
     if(game.invincible()&&lisaShield&&int((virtualTime/170)%2)!=lisaFirstPhase&&!lisaDance){lisaDance=true;if(!capture("supply-run-lisa-dance"))return 64;}
     const SupplyRun::Obstacle* next=nullptr;
     for(const auto& obstacle:game.obstacles()){
         if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
         if(!next||obstacle.x<next->x)next=&obstacle;
     }
     if(next&&next->kind>=3&&!captured[next->kind]&&next->x<650&&next->x>420){
         captured[next->kind]=true;
         if(!capture("easter-egg-"+std::to_string(next->kind)))return 23;
     }
     if(next&&game.ducking()&&next->x>120&&next->x<160){
         if(next->kind==3&&!duckGull){duckGull=true;if(!capture("supply-run-duck-seagull"))return 30;}
         if(next->kind==7&&!duckPlane){duckPlane=true;if(!capture("supply-run-duck-plane"))return 31;}
     }
     if(std::all_of(captured.begin()+3,captured.end(),[](bool value){return value;})&&duckGull&&duckPlane&&lisaPickup&&lisaShield&&lisaDance)break;
 }
 if(!std::all_of(captured.begin()+3,captured.end(),[](bool value){return value;})||!duckGull||!duckPlane||!lisaPickup||!lisaShield||!lisaDance)return 24;
#endif
 // The sidebar/game-toggle locations have no navigation routes during work.
 for(const auto& point:{std::pair{100,160},std::pair{560,51}}){
     ui.touchAction({true,true,point.first,point.second});
     if(ui.touchAction({true,false,point.first,point.second}))return 18;
 }
 ui.setSupplyRunEnabled(false);
 ui.installLoading("Installing SD tester ZIP","Copying verified files",74);
 if(ui.waitingGameActive()||!ui.operationScreenActive())return 47;
 ui.waitingGameInput(button::X);SDL_Delay(320);ui.animateDownloadWaiting();
 if(!ui.waitingGameActive())return 48;
 ui.installLoading("Installing SD tester ZIP","File 23 of 101: zm_tomb.ipak\n1.7 GiB of 4.43 GiB installed\nElapsed 4 min 10 sec  |  About 6 min remaining",74);
 if(!capture("install-fullscreen"))return 19;
 if(!ui.operationScreenActive()||!tap(1070,690,TouchActionKind::Button,int(button::X)))return 40;
 ui.waitingGameInput(button::X);
 SDL_Delay(150);ui.animateDownloadWaiting();
 if(!capture("install-game-transition"))return 49;
 SDL_Delay(180);ui.animateDownloadWaiting();
 if(ui.waitingGameActive()||!ui.operationScreenActive()||ui.supplyRunEnabled())return 41;
 if(!capture("install-game-hidden"))return 42;
 if(!tap(640,690,TouchActionKind::Button,int(button::X)))return 43;
 ui.waitingGameInput(button::X);SDL_Delay(320);ui.animateDownloadWaiting();
 if(!ui.waitingGameActive()||!ui.operationScreenActive()||!ui.supplyRunEnabled())return 44;
 if(!capture("install-game-restored"))return 45;

 // Noncancellable installation never exposes a Back/Exit touch control.
 ui.touchAction({true,true,1100,690});
 const auto safe=ui.touchAction({true,false,1100,690});
 if(safe&&safe->kind==TouchActionKind::Button&&(safe->value&int(button::B|button::Plus)))return 20;
 // A normal loading sequence must immediately stop the runner, even if its
 // title contains words such as package, download or install.
 for(const char* title:{"Loading Unbound HQ","Checking map packs","Checking saved package","Loading install settings"}){
     ui.loading(title,"Preparing the requested screen...",40,false);
     if(ui.waitingGameActive()||ui.operationScreenActive())return 26;
     ui.waitingGameInput(button::X);
     if(!ui.supplyRunEnabled())return 46;
 }
 if(!capture("normal-loading-centered"))return 27;
 if(tap(500,490,TouchActionKind::Button,int(button::A)))return 28;
 ui.loading("Checking saved package","Verifying the saved ZIP before use.",40,true,"Cancel verification");
 if(ui.waitingGameActive()||!tap(640,690,TouchActionKind::Button,int(button::B)))return 29;
 ui.setNavigation(4,false);
 ui.support("UNBOUND HQ SUPPORT\nHQ version: 1.5 / r66\nGame: Black Ops II\n\nSUPPORT EXPORT\nHQ reports and available Wii U crash logs are saved together.\nConsole log source: /storage_slc/sys/logs/\nZIP: sd:/wiiu/apps/Unbound-HQ/support-report.zip\nOriginal console logs are kept.\n",0,"");
 if(!capture("support-export-zip")||!tap(150,690,TouchActionKind::Button,int(button::A)))return 76;
 ui.modal("Install complete","Your update is ready. Restart your console to load the updated plugins.",false,"OK");
 if(ui.waitingGameActive()||!capture("dialog"))return 21;
 ui.shutdown();
 std::cout<<"Actual UI screenshots and touch routes passed, including fullscreen installation isolation\n";
 return 0;
}
