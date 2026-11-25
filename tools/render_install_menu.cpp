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

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: render-install ABSOLUTE_OUTPUT_DIRECTORY\n";
        return 2;
    }
    const std::filesystem::path output = argv[1];
    std::filesystem::create_directories(output);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    unbound::Ui ui;
    std::string error;
    if (!ui.initialize(error)) {
        std::cerr << "Actual UI initialization failed: " << error << '\n';
        ui.shutdown();
        return 1;
    }
    if (TTF_Font* font = TTF_OpenFont("fs:/vol/content/fonts/NotoSans-Regular.ttf", 18)) {
        for (const std::string value : {
                 "Europe (Spanish/Italian) • 2 detected installations",
                 "Europe (Spanish/Italian) • 10 detected installations",
                 "Europe (Spanish/Italian) • Saved location unavailable",
                 "Europe (Spanish/Italian) • 10 locations",
                 "Europe (Spanish/Italian) • Unavailable"}) {
            int width = 0, height = 0;
            TTF_SizeUTF8(font, value.c_str(), &width, &height);
            std::cout << "SUBTITLE " << width << "px: " << value << '\n';
        }
        TTF_CloseFont(font);
    }
    bool ok = true;
    const auto render = [&](const std::string& name, const unbound::GameInstall& active,
                            const std::vector<unbound::InstallOption>& options,
                            const unbound::InstallPreferences& draft, int selectedRow,
                            const std::string& notice = {}, bool contentAllowed = true,
                            bool updatePlan = false, bool reinstallAvailable = true,
                            const std::string& latest = "0.0.05P2.1") {
        ui.setNavigation(1,false);
        ui.installMenu(active, options, draft, selectedRow, latest, notice,
                       contentAllowed, updatePlan, reinstallAvailable);
        ok = screenshot(output / (name + ".png")) && ok;
    };

    const auto usaUsb = fixtureInstall(&unbound::SupportedSkus[0], unbound::Device::USB,
                                       unbound::Device::USB, "english");
    const std::vector<unbound::InstallOption> usaOptions{{usaUsb, {"english", "french"}}};
    render("install-auto-current", usaUsb, usaOptions, {}, 0);
    render("install-auto-reinstall", usaUsb, usaOptions, {}, 3);
    auto inheritedRepair=usaUsb;inheritedRepair.modVersion="0.2.0";
    ui.installMenu(inheritedRepair,{{inheritedRepair,{"english","french"}}},{},3,"0.2.0",
        "Original game files need restoration. Existing Unbound files will be kept.",true,false,false,true);
    ok=screenshot(output/"install-original-content-repair.png")&&ok;
    ui.modal("Restore original game files?",
        "An earlier HQ build left out original files while preparing an update folder. Restore those files from your own game installation. Existing mod files are kept.\n\n"
        "Local copy: 4.28 GiB.\nFree space needed: 4.29 GiB.\nAvailable on game drive: 183.0 GiB.\n\n"
        "No game package needs to be downloaded for this repair. Large video folders can take several minutes. Keep the game closed until this finishes.",
        false,"RESTORE FILES",0,true);
    ok=screenshot(output/"original-content-repair-confirmation.png")&&ok;

    auto europe = fixtureInstall(&unbound::SupportedSkus[1], unbound::Device::MLC,
                                 unbound::Device::Disc, "French");
    europe.targetOverridden = true;
    europe.languageOverridden = true;
    unbound::InstallPreferences french;
    french.updateDevice = unbound::Device::MLC;
    french.updateTitleId = europe.sku->updateTitleId;
    french.languageFolder = "French";
    render("install-manual-europe", europe, {{europe, {"english", "French"}}}, french, 1,
           "Selection changed. Save before installing or leaving this screen.");

    auto longVersion = europe;
    longVersion.modVersion = "0.0.05P20.123-beta.1234";
    render("install-long-versions", longVersion, {{longVersion, {"english", "French"}}},
           french, 3, {}, true, true, true, "0.0.05P21.123-beta.1234");

    auto japanese = fixtureInstall(&unbound::SupportedSkus[4], unbound::Device::USB,
                                  unbound::Device::USB, "fulljap");
    render("install-japan-fulljap", japanese, {{japanese, {"fulljap"}}}, {}, 1);

    auto pending = europe;
    french.languageInstallPending = true;
    render("install-language-pending", pending, {{europe, {"english", "French"}}}, french,
           3, "Saved. A full install is needed to populate the selected language.", true, true);

    auto missing = usaUsb;
    missing.base.present = false;
    missing.update.present = false;
    missing.destinationReady = false;
    missing.targetOverridden = true;
    missing.modVersion.clear();
    missing.destinationWarning = "The saved USB destination is unavailable. Reconnect it, select another installation, or reset to Auto.";
    unbound::InstallPreferences savedUsb;
    savedUsb.updateDevice = unbound::Device::USB;
    savedUsb.updateTitleId = usaUsb.sku->updateTitleId;
    render("install-saved-usb-unavailable", missing, {}, savedUsb, 0, {}, true, false, false);

    auto missingLanguage = europe;
    missingLanguage.destinationReady = false;
    missingLanguage.destinationWarning = "The saved French language folder is unavailable. Choose one of the language folders found in this installation, or reset to Auto.";
    render("install-saved-language-unavailable", missingLanguage,
           {{missingLanguage, {"english"}}}, french, 1, {}, true, false, false);

    auto spanishItalian = fixtureInstall(&unbound::SupportedSkus[2], unbound::Device::USB,
                                         unbound::Device::USB, "spanish");
    auto spanishInternal = fixtureInstall(&unbound::SupportedSkus[2], unbound::Device::MLC,
                                          unbound::Device::MLC, "italian");
    render("install-two-europe-locations", spanishItalian,
           {{spanishItalian, {"spanish", "italian"}},
            {spanishInternal, {"spanish", "italian"}}}, {}, 0);

    auto missingEurope = spanishItalian;
    missingEurope.base.present = false;
    missingEurope.update.present = false;
    missingEurope.destinationReady = false;
    missingEurope.targetOverridden = true;
    missingEurope.modVersion.clear();
    missingEurope.destinationWarning = "The saved USB destination is unavailable. Reconnect it, select another installation, or reset to Auto.";
    savedUsb.updateTitleId = spanishItalian.sku->updateTitleId;
    render("install-europe-unavailable", missingEurope,
           {{spanishInternal, {"spanish", "italian"}}}, savedUsb, 0, {}, true, false, false);

    render("install-session-blocked", usaUsb, usaOptions, {}, 3,
           "Restart HQ and complete its app update before installing game content.", false);

    auto outdated = usaUsb;
    outdated.update.titleVersion = 64;
    render("install-official-outdated", outdated, {{outdated, {"english", "french"}}}, {}, 3);
    auto unknownOfficial = usaUsb;
    unknownOfficial.update.titleVersion = 0;
    render("install-official-unknown", unknownOfficial,
           {{unknownOfficial, {"english", "french"}}}, {}, 3);

    unbound::GameInstall notDetected;
    notDetected.destinationReady = false;
    notDetected.languageFolder.clear();
    notDetected.destinationWarning = "No Black Ops II installation was found. Insert a supported disc or reconnect your game storage, then rescan.";
    render("install-no-game-found", notDetected, {}, {}, 0, {}, true, false, false);
    const auto capture=[&](const std::string& name){ok=screenshot(output/(name+".png"))&&ok;};
    const auto tap=[&](const char* name,int x,int y,std::optional<TouchAction> expected){
        ui.touchAction({true,false,0,0});
        const auto down=ui.touchAction({true,true,x,y});
        const auto action=ui.touchAction({true,false,0,0});
        if(down||action!=expected){std::cerr<<"Touch map mismatch: "<<name<<'\n';ok=false;}
    };
    const auto expected=[](TouchActionKind kind,int value){return std::optional<TouchAction>{{kind,value}};};
    const std::string news="# Grind and Project KENNEL Have Arrived\n\n"
        "## Included in this release\n- **Grind MP map**\n- Project KENNEL\n- Mod loader\n- FOV slider\n\n"
        "All with **Protarium support online!**\n\n"
        "Project KENNEL has lost control. Across the quarantine zone, military attack dogs obey a corrupted command: hunt until nothing remains.\n\n"
        "Fight the hordes with up to four soldiers. Scavenge supplies, upgrade your weapons, and keep your squad alive against relentless packs.\n\n"
        "## Improvements\n- Theater mode\n- Faster downloads and installs\n- Controller navigation throughout HQ\n";
    ui.prepareNews(news);
    Release currentRelease;currentRelease.version=usaUsb.modVersion;
    ui.setNavigation(0,false);
    ui.home(usaUsb,&currentRelease,news,4,"Release information is current");capture("home-dashboard");
    for(int section=0;section<5;++section)
        tap("dashboard sidebar",42,194+section*64,expected(TouchActionKind::Sidebar,section));
    tap("home news",400,310,expected(TouchActionKind::HomeAction,0));
    tap("home launch",1080,472,expected(TouchActionKind::HomeAction,4));
    tap("home install",1080,584,expected(TouchActionKind::HomeAction,1));
    ui.cancelTouchGesture();
    tap("controller reset preserves hitmap",42,194,expected(TouchActionKind::Sidebar,0));
    ui.touchAction({true,true,400,310});
    ui.touchAction({true,true,440,310});
    if(ui.touchAction({true,false,0,0})){std::cerr<<"Dragged contact activated news\n";ok=false;}
    ui.touchAction({true,true,400,310});
    ui.news(news,0);
    if(ui.touchAction({true,false,0,0})){std::cerr<<"Held contact crossed page boundary\n";ok=false;}
    ui.home(usaUsb,nullptr,news,1,"Release check unavailable");capture("home-feed-unavailable");
    ui.home(usaUsb,nullptr,news,1,"Release check unavailable",true,true);capture("home-repair-needed");
    ui.home(usaUsb,&currentRelease,news,4,{},false,true);capture("home-restart-required");
    ui.home(usaUsb,&currentRelease,news,0,"Using cached release information");capture("home-cached-release");
    ui.home(longVersion,&currentRelease,news,4,"Using cached release information");capture("home-europe-long-version");
    ui.home(spanishItalian,&currentRelease,news,4,"Release information is current");capture("home-europe-spanish");
    ui.home(japanese,&currentRelease,news,4,"Release information is current");capture("home-japan");
    ui.home(notDetected,nullptr,news,4,"Release check unavailable");capture("home-no-game-found");
    auto missingLong=missingEurope;missingLong.modVersion=longVersion.modVersion;
    ui.home(missingLong,nullptr,news,1,"Using cached release information",true,true);capture("home-missing-europe-long-warning");
    for(int section=0;section<5;++section){
        ui.setNavigation(section,true);
        ui.home(usaUsb,&currentRelease,news,-1,"Release information is current");
        capture(section==0?"home-menu-focused":"home-menu-focused-"+std::to_string(section));
    }
    ui.setNavigation(0,false);
    ui.news(news,0);capture("whats-new-top");
    // The preceding Home -> News publish itself must require an observed
    // release, even when no old press was polled during synchronous work.
    ui.touchAction({true,true,42,194});
    if(ui.touchAction({true,false,0,0})){std::cerr<<"Unseen held press activated a new page\n";ok=false;}
    tap("news rail",42,194,expected(TouchActionKind::Sidebar,0));
    tap("news up",260,688,expected(TouchActionKind::Button,static_cast<int>(button::Up)));
    tap("news down",312,688,expected(TouchActionKind::Button,static_cast<int>(button::Down)));
    ui.news(news,ui.newsMaximumScroll());capture("whats-new-bottom");
    ui.loading("Downloading Unbound 0.0.05P2.1",
        "Package 1 of 2 • Full release\n\n128.0 MiB of 195.4 MiB\nDownload speed: 1.6 MiB/s\nTime remaining: about 42 seconds\nSaved download can be resumed if canceled.",66,true);
    capture("download-progress-cancel");
    tap("download cancellation",640,689,expected(TouchActionKind::Button,static_cast<int>(button::B)));
    ui.touchAction({true,true,640,689});
    ui.loading("Downloading Unbound 0.0.05P2.1","130 MiB downloaded. Time remaining: about 40 seconds",67,true);
    if(ui.touchAction({true,false,0,0})!=expected(TouchActionKind::Button,static_cast<int>(button::B))){
        std::cerr<<"Stable download progress redraw reset a valid Cancel tap\n";ok=false;
    }
    ui.loading("Downloading Unbound 0.0.05 base (1/2)",
        "7.8 MB of 195.4 MB\nTime remaining: 2m 01s  |  1.56 MiB/s",4,true);
    capture("download-progress-compact");
    ui.loading("Downloading Unbound 0.0.05P20.123-beta.1234 cumulative update package (2/12) for the selected game language",
        "120 MiB of 195 MiB\nTime remaining: 2m 01s  |  1.56 MiB/s",62,true);
    capture("download-long-title");
    ui.loading("Installing Unbound 0.0.05P2.1",
        "Package 1 of 2 • Full release\n\n462 MiB of 686 MiB unpacked\nTime remaining: about 35 seconds\nDestination: USB storage • english\nFinishing files safely. Please do not power off.",67,false);
    capture("install-progress-safe");
    tap("install cannot cancel",640,689,std::nullopt);
    ui.modal("Install selected release?",
        "Destination: USB storage / USA\nLanguage: english\nInstalled: 0.0.05P2.1\n\nHQ will install the full release and its patches. Your settings and unrelated mod folders are preserved.",false,"INSTALL",0,true);
    capture("confirmation");
    tap("confirm install",300,689,expected(TouchActionKind::Button,static_cast<int>(button::A)));
    tap("cancel confirmation",950,689,expected(TouchActionKind::Button,static_cast<int>(button::B)));
    ui.setNavigation(1,false);
    ui.installMenu(usaUsb,usaOptions,{},0,currentRelease.version,{},true,false,true);
    tap("install destination focus",300,280,expected(TouchActionKind::InstallRow,0));
    tap("previous destination",550,280,expected(TouchActionKind::InstallChoice,0));
    tap("next destination",606,280,expected(TouchActionKind::InstallChoice,1));
    tap("install language focus",300,365,expected(TouchActionKind::InstallRow,1));
    tap("previous language",550,365,expected(TouchActionKind::InstallChoice,2));
    tap("next language",606,365,expected(TouchActionKind::InstallChoice,3));
    tap("save selection",300,462,expected(TouchActionKind::InstallRow,2));
    tap("install selected",300,527,expected(TouchActionKind::InstallRow,3));
    tap("reset auto",300,594,expected(TouchActionKind::InstallRow,4));
    ui.installMenu(missing,{},savedUsb,3,{}, {},true,false,false);
    tap("disabled install row",300,527,std::nullopt);
    tap("disabled install footer A",140,689,std::nullopt);
    std::string longRecovery;
    for(int i=0;i<14;++i)longRecovery+="Recovery step "+std::to_string(i+1)+": Keep the SD card connected and retry the unfinished package. HQ checks the package hash before reusing its cached download.\n\n";
    const int recoveryMaximum=ui.modal("Installation incomplete",longRecovery,true,"CONTINUE");
    capture("recovery-message-top");
    ui.modal("Installation incomplete",longRecovery,true,"CONTINUE",recoveryMaximum);
    capture("recovery-message-bottom");

    WorkshopPlugin mic{"GAMEPAD_MIC_REDIRECT","GamePad Mic Redirect","Tonytrawl",
        "Use the GamePad microphone with compatible games without an external USB microphone. Controls and compatibility notes are maintained by the developer.",
        "https://example.invalid/releases.txt","https://example.invalid/",
        {"wiiu/environments/aroma/plugins/gamepad_mic_redirect.wps"}};
    WorkshopPlugin link{"UNBOUND_MOD_LINK","Unbound Mod Link","Black Ops 2 Unbound",
        "Load your SD mods directly from unbound/content/mods. Nothing is copied into installed game content.",
        "https://example.invalid/releases.txt","https://example.invalid/",
        {"wiiu/environments/aroma/plugins/UnboundModLink.wps"}};
    WorkshopPlugin protarium{"PROTARIUM","Protarium Network","Protarium Network",
        "Connect compatible Wii U games to Protarium Network. Inkay changes network and SSL behavior; restart the console after installing or updating.",
        "https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt",
        "https://github.com/Protarium-Network/Inkay-GitHub-Release",
        {"wiiu/environments/aroma/modules/Inkay-pretendo.wms",
         "wiiu/environments/aroma/plugins/Inkay-pretendo.wps"}};
    PluginInstallStatus micStatus;micStatus.presence=PluginPresence::Managed;
    micStatus.installedRevision=3;micStatus.installedVersion="1.0.3";
    PluginInstallStatus linkStatus;linkStatus.presence=PluginPresence::Managed;
    linkStatus.installedRevision=7;linkStatus.installedVersion="0.0.07";
    ui.setNavigation(2,false);
    ui.pluginCatalog({mic,link,protarium},{micStatus,linkStatus,{}},{true,true,false},0,0,{}, {false,true,false});
    capture("workshop-installed-feed-unavailable");
    tap("plugin listing",450,270,expected(TouchActionKind::PluginRow,0));
    ui.pluginCatalog({mic,link,protarium},{micStatus,linkStatus,{}},{true,true,false},0,180,{}, {false,true,false});
    tap("clipped first plugin",450,103,expected(TouchActionKind::PluginRow,0));
    tap("clipped header not plugin",450,101,std::nullopt);
    ui.pluginDetails(mic,nullptr,micStatus,true,0,0,{},"Latest release check unavailable. Your installed plugin is unchanged.");
    capture("plugin-feed-unavailable");
    PluginRelease micRelease;micRelease.revision=3;micRelease.version="1.0.3";
    ui.pluginDetails(mic,&micRelease,micStatus,true,1,0,"Restart the console/Aroma environment after installing this plugin.","Release record ready.");
    capture("plugin-current");
    tap("plugin install",440,620,expected(TouchActionKind::PluginAction,0));
    tap("plugin auto update",1000,620,expected(TouchActionKind::PluginAction,1));
    ui.pluginDetails(mic,&micRelease,micStatus,true,1,0,{},"Using cached release data; publisher feed: unable to resolve host");
    capture("plugin-cached-release");
    ui.pluginDetails(mic,&micRelease,micStatus,true,1,0,{},"Publisher reused revision 3 with different metadata; HQ retained the cached record.");
    capture("plugin-cached-reused-revision");
    mic.description.clear();
    for(int i=0;i<40;++i)mic.description+="Long description paragraph "+std::to_string(i+1)+": The complete description remains readable, including compatibility, setup instructions, and controls.\n\n";
    ui.pluginDetails(mic,&micRelease,micStatus,true,1,0,{},"Release record ready.");
    capture("plugin-long-description-top");
    const int pluginMaximum=ui.pluginDetailsMaximumScroll();
    ui.pluginDetails(mic,&micRelease,micStatus,true,1,pluginMaximum,{},"Release record ready.");
    capture("plugin-long-description-bottom");
    if(pluginMaximum<=0){std::cerr<<"Long plugin text was not scrollable\n";ok=false;}

    // Protarium uses the same available/manual/managed/offline states and
    // persistent actions as every supported startup-auto-update plugin.
    // Metadata is only a rendering fixture; no network or plugin writes occur.
    PluginRelease protariumRelease;protariumRelease.pluginKey=protarium.key;
    protariumRelease.revision=12;protariumRelease.version="v3.1.0-protarium-jd";
    PluginInstallStatus protariumNotInstalled;
    PluginInstallStatus protariumCurrent;protariumCurrent.presence=PluginPresence::Managed;
    protariumCurrent.installedRevision=12;
    protariumCurrent.installedVersion=protariumRelease.version;
    PluginInstallStatus protariumOutdated=protariumCurrent;
    protariumOutdated.installedRevision=11;
    protariumOutdated.installedVersion="v3.0.0-protarium-jd";
    protariumOutdated.updateAvailable=true;
    PluginInstallStatus protariumManual;protariumManual.presence=PluginPresence::Unmanaged;
    mic.description="Use the GamePad microphone with compatible games without an external USB microphone.";
    const auto protariumCatalog=[&](const char* name,const PluginInstallStatus& state,
                                   bool ready,const std::string& notice={}){
        ui.pluginCatalog({mic,link,protarium},{micStatus,linkStatus,state},
                         {true,true,true},2,0,notice,{true,true,ready});
        capture(name);
        tap("Protarium catalog selection",450,550,expected(TouchActionKind::PluginRow,2));
    };
    protariumCatalog("workshop-protarium-available",protariumNotInstalled,true);
    protariumCatalog("workshop-protarium-current",protariumCurrent,true);
    protariumCatalog("workshop-protarium-update",protariumOutdated,true);
    protariumCatalog("workshop-protarium-manual",protariumManual,true);
    protariumCatalog("workshop-protarium-offline",protariumCurrent,false,
                     "Latest release check unavailable. Installed plugins are unchanged.");
    const auto protariumDetails=[&](const char* name,const PluginInstallStatus& state,
                                   const PluginRelease* release,bool autoUpdate,
                                   const std::string& feed){
        ui.pluginDetails(protarium,release,state,autoUpdate,1,0,{},feed);
        capture(name);
        tap("Protarium install action",440,620,release?
            expected(TouchActionKind::PluginAction,0):std::nullopt);
        tap("Protarium auto-update action",1000,620,expected(TouchActionKind::PluginAction,1));
    };
    protariumDetails("plugin-protarium-available-auto-off",protariumNotInstalled,&protariumRelease,false,"Release record ready.");
    protariumDetails("plugin-protarium-current-auto-off",protariumCurrent,&protariumRelease,false,"Release record ready.");
    protariumDetails("plugin-protarium-current-auto-on",protariumCurrent,&protariumRelease,true,"Release record ready.");
    protariumDetails("plugin-protarium-update",protariumOutdated,&protariumRelease,true,"Release record ready.");
    protariumDetails("plugin-protarium-manual",protariumManual,&protariumRelease,false,"Release record ready.");
    protariumDetails("plugin-protarium-offline-auto-on",protariumCurrent,nullptr,true,"Latest release check unavailable. Your installed plugin is unchanged.");
    protariumDetails("plugin-protarium-offline-auto-off",protariumCurrent,nullptr,false,"Latest release check unavailable. Your installed plugin is unchanged.");
    protariumDetails("plugin-protarium-cached",protariumCurrent,&protariumRelease,false,"Using cached release data; publisher feed: unable to resolve host");
    ui.pluginDetails(protarium,&protariumRelease,protariumCurrent,false,1,
                     ui.pluginDetailsMaximumScroll(),{},"Release record ready.");
    capture("plugin-protarium-restart-safety");
    tap("Protarium scrolled install",440,620,expected(TouchActionKind::PluginAction,0));
    tap("Protarium scrolled auto-update",1000,620,expected(TouchActionKind::PluginAction,1));

    LocalModInventoryItem shared;shared.folder="kennel";shared.hasSd=true;shared.hasConsole=true;
    shared.sd={"kennel","Project KENNEL","Face endless waves of corrupted military attack dogs. Hold out as a squad and prepare for extraction.","Unbound Team",{},false};
    shared.console=shared.sd;shared.console.installed=true;
    LocalModInventoryItem console;console.folder="hud";console.hasConsole=true;
    console.console={"hud","Clean HUD","A quieter interface for Black Ops II.","Community",{},true};
    ui.setNavigation(3,false);
    ui.localModManager({shared,console},0,0,{},true,Device::USB);capture("mod-manager-shared");
    tap("mod row selection only",290,255,expected(TouchActionKind::LocalModRow,0));
    tap("second mod row",290,340,expected(TouchActionKind::LocalModRow,1));
    tap("Mod Link toggle",1040,145,expected(TouchActionKind::Button,static_cast<int>(button::Minus)));
    ui.touchAction({true,true,290,255});
    auto swapped=shared;swapped.folder="different-folder";
    ui.localModManager({swapped,console},0,0,{},true,Device::USB);
    if(ui.touchAction({true,false,0,0})){std::cerr<<"Mod index activated a replacement folder\n";ok=false;}
    shared.sd.description.clear();
    for(int i=0;i<35;++i)shared.sd.description+="Mod notes "+std::to_string(i+1)+": This complete paragraph explains the mod controls and installation requirements.\n\n";
    ui.localModManager({shared,console},0,0,{},true,Device::USB);capture("mod-manager-long-description-top");
    tap("mod detail scroll up",1218,251,expected(TouchActionKind::Button,static_cast<int>(button::Left)));
    tap("mod detail scroll down",1218,584,expected(TouchActionKind::Button,static_cast<int>(button::Right)));
    const int localMaximum=ui.localModDetailsMaximumScroll();
    ui.localModManager({shared,console},0,0,{},true,Device::USB,localMaximum);capture("mod-manager-long-description-bottom");
    if(localMaximum<=0){std::cerr<<"Long local mod text was not scrollable\n";ok=false;}
    ui.localModManager({},0,0,{},false,Device::USB);capture("mod-manager-empty");
    const std::string report="BLACK OPS 2 UNBOUND HQ\n\n"
        "App version: 0.0.22-beta • r22\nConsole region: USA\nDetected game: Black Ops II / USA\n"
        "Base storage: USB\nUpdate storage: USB\nOfficial update: v128 - current\nLanguage: english\n"
        "Install destination: Automatic\nInstalled Unbound: 0.0.05P2.1\n\n"
        "DOWNLOAD PERFORMANCE\nLast package: Full release\nDownloaded bytes: 204936880\nTime: 132814 ms\nMode: pipeline\n\n"
        "INSTALL PERFORMANCE\nInstalled bytes: 719721992\nUnpack time: 30629 ms\nWrite time: 63984 ms\n\n"
        "RECOVERY\nIncomplete package: None\nCached package: Verified\n\n"
        "STORAGE\nSD free: 18.6 GB\nUSB free: 121.0 GB\nInternal free: 5.1 GB\n";
    ui.setNavigation(4,false);
    ui.support(report,0);capture("support-top");
    ui.support(report,ui.supportMaximumScroll());capture("support-bottom");
    if(SDL_Window* window=SDL_GetWindowFromID(1)){
        SDL_SetWindowSize(window,854,480);
        ui.setNavigation(0,false);
        ui.home(usaUsb,&currentRelease,news,4,"Release information is current");
        capture("home-gamepad-scaled");
        ui.setNavigation(4,true);
        ui.home(usaUsb,&currentRelease,news,-1,"Release information is current");
        capture("sidebar-gamepad-scaled");
        ui.loading("Downloading Unbound 0.0.05 base (1/2)",
            "7.8 MB of 195.4 MB\nTime remaining: 2m 01s  |  1.56 MiB/s",4,true);
        capture("download-progress-gamepad-scaled");
        ui.installMenu(inheritedRepair,{{inheritedRepair,{"english","french"}}},{},3,"0.2.0",
            "Original game files need restoration. Existing Unbound files will be kept.",true,false,false,true);
        capture("install-original-content-repair-gamepad");
    }
    if(ok)std::cout<<"Actual UI touch hit maps, clipping, disabled actions, drag and boundary safety passed.\n";
    ui.shutdown();
    return ok ? 0 : 1;
}
