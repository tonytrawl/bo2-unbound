#pragma once

#include "model.hpp"
#include "map_packs.hpp"
#include "network_stats.hpp"
#include "network_globe.hpp"
#include "touch.hpp"
#include "background_music.hpp"
#include "supply_run.hpp"
#include "supply_run_display.hpp"
#include "quick_update.hpp"
#include "package_layout.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace unbound {

enum class WorkshopTab { Plugins, CustomMaps, Mods, CustomPackages };

class Ui {
public:
#ifdef HQ_LAYOUT_PREVIEW
    void installLayoutPreview(int option);
    const SupplyRun& waitingGamePreviewState() const { return supplyRun_; }
    void waitingGamePreviewFrame(std::uint32_t now) { renderWaitingGameFrame(now); }
#endif
    static constexpr int NavigationRailWidth = 226;
    static constexpr int ContentLeft = NavigationRailWidth + 18;
    static constexpr int ContentRight = 1252;
    static constexpr int LocalModListTop = 225;
    static constexpr int LocalModRowHeight = 82;
    static constexpr SDL_Rect ContentQueueViewport{244,266,606,292};
    static constexpr int PluginCatalogListTop = 234;
    static constexpr int PluginCatalogRowHeight = 142;
    bool initialize(std::string& error);
    void shutdown();
    void pump();
    void bootScreen();
    // 0 HQ, 1 Install / Update, 2 Workshop, 3 Mod Manager, 4 Support.
    // The rail owns focus only on the Dashboard; child pages retain their
    // ordinary controller controls and show a quieter section indicator.
    void setNavigation(int selected, bool focused);
    // Tap hit maps are registered from the same rectangles the UI draws.
    // Moving/resizing content or changing screen cancels an in-flight press.
    std::optional<TouchAction> touchAction(const TouchSample& sample);
    void invalidateTouch();
    void cancelTouchGesture();
    void setNewsImages(const std::vector<NewsImage>& images);
    void prepareNews(const std::string& changes);
    void loading(const std::string& title, const std::string& detail, int progress = -1,
                 bool cancellable = false, const char* cancelLabel = "Cancel download");
    // Only real transfer/install callers opt into the waiting game.
    void downloadLoading(const std::string& title, const std::string& detail, int progress = -1,
                         bool cancellable = true, const char* cancelLabel = "Cancel download");
    void installLoading(const std::string& title, const std::string& detail, int progress = -1);
    // Called by the main-thread download wait loop, independently of transfer
    // callbacks. Reuses a static screen texture and a small code-defined pixel atlas.
    void animateDownloadWaiting();
    bool waitingGameActive() const { return downloadAnimationActive_; }
    bool operationScreenActive() const { return operationScreenActive_; }
    bool supplyRunEnabled() const { return supplyRunDisplay_.enabled(); }
    void setSupplyRunEnabled(bool enabled) { supplyRunDisplay_.restore(enabled); }
    void waitingGameInput(std::uint32_t buttons, std::uint32_t held = 0, TouchSample touch = {});
    int modal(const std::string& title, const std::string& body, bool isError = false,
              const std::string& actionLabel = "CONTINUE", int scrollOffset = 0,
              bool confirm = false);
    void home(const GameInstall& install, const Release* release, const std::string& changes,
              int selectedAction, const std::string& updateStatus,
              bool repairNeeded = false, bool restartRequired = false, int gameChoice = -1, unsigned mapUpdates = 0);
    void installMenu(const GameInstall& active, const std::vector<InstallOption>& options,
                     const InstallPreferences& draft, int selectedRow,
                     const std::string& latest, const std::string& notice,
                     bool contentAllowed, bool hasUpdatePlan, bool reinstallAvailable,
                     bool inheritedRepairNeeded = false);
    void quickUpdate(const GameInstall& install, const std::vector<QuickUpdateRow>& rows,
                     bool coreCurrent = false, int scroll = 0);
    void installReview(const GameInstall& active, const std::string& latest,
                       const std::string& notice, int selected, bool enabled,
                       bool sdAvailable, bool repair, bool update, bool mapsAvailable = false);
    void contentQueue(const std::vector<std::pair<std::string,std::string>>& rows,
                      const GameInstall& install, const std::string& total, int scroll);
    void mapPacks(const std::vector<MapPack>& packs, const std::vector<bool>& selected,
                  const std::vector<std::string>& versions, int focused,
                  const GameInstall& install, const std::string& notice, bool reinstall = false);
    void news(const std::string& changes, int scrollOffset);
    void network();
    void setNetwork(const NetworkStats& stats,float yaw,float pitch){networkStats_=stats;globeYaw_=yaw;globePitch_=pitch;}
    int newsMaximumScroll() const { return newsMaximumScroll_; }
    // Report text is laid out only when it changes; scrolling reuses its
    // bounded-height textures instead of rebuilding a long document per frame.
    int support(const std::string& report, int scrollOffset,
                const std::string& status = {});
    int supportMaximumScroll() const { return supportMaximumScroll_; }
    void localModManager(const std::vector<LocalModInventoryItem>& mods,
                         std::size_t selected, int scrollOffset,
                         const std::string& notice, bool modLinkEnabled,
                         Device consoleDevice = Device::Missing, int detailsScroll = 0);
    int localModDetailsMaximumScroll() const { return localModDetailsMaximumScroll_; }
    void pluginCatalog(const std::vector<WorkshopPlugin>& plugins,
                       const std::vector<PluginInstallStatus>& statuses,
                       const std::vector<bool>& checked,
                       std::size_t selected, int scrollOffset,
                       const std::string& catalogNotice,
                       const std::vector<bool>& releaseReady = {});
    int pluginCatalogMaximumScroll() const { return pluginCatalogMaximumScroll_; }
    void workshopContent(WorkshopTab tab,const std::vector<MapPack>& maps = {},
                            const std::vector<std::string>& installed = {},
                            std::size_t selected = 0,int scrollOffset = 0,
                            const std::string& notice = {},bool installReady = false);
    void localPackages(const std::string& directory,const std::vector<SdPackageEntry>& entries,
                       std::size_t selected,int scroll,const std::string& notice,bool resuming=false);
    void pluginDetails(const WorkshopPlugin& plugin, const PluginRelease* release,
                       const PluginInstallStatus& status, bool autoUpdateEnabled,
                       int selectedAction, int scrollOffset, const std::string& notice,
                       const std::string& feedStatus);
    int pluginDetailsMaximumScroll() const { return pluginDetailsMaximumScroll_; }
    void workshopCatalog(const std::vector<WorkshopMod>& mods, std::size_t featured,
                         std::size_t selected, int scrollOffset, bool featuredFocus);
    void modDetails(const WorkshopMod& mod, const std::vector<Review>& reviews,
                    int selectedAction, int scrollOffset, const std::string& notice,
                    const std::string& userName);
    void commentEditor(const WorkshopMod& mod, const std::string& draft,
                       int selectedRow, int selectedColumn);

private:
    NetworkStats networkStats_;
    NetworkGlobe networkGlobe_;
    float globeYaw_=0,globePitch_=0;
    void networkSummary(bool focused);
    BackgroundMusic backgroundMusic_;
    struct CachedText {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };
    struct NewsBlock {
        SDL_Texture* texture = nullptr;
        SDL_Rect rect{};
        int kind = 0; // 0 texture, 1 divider, 2 raised panel, 3 orange pill
    };
    struct SupportBlock {
        CachedText text;
        int y = 0;
    };
    struct FlowChunk {
        std::string source;
        CachedText text;
        int y = 0;
    };
    struct FlowDocument {
        std::vector<FlowChunk> chunks;
        int height = 0;
    };
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* boot_ = nullptr;
    SDL_Texture* logo_ = nullptr;
    SDL_Texture* squareLogo_ = nullptr;
    SDL_Texture* newsHero_ = nullptr; // borrowed from newsImages_, never separately destroyed
    SDL_Texture* waitingAtlas_ = nullptr;
    SupplyRun supplyRun_;
    TouchRect waitingDuckTouch_{};
    SDL_Texture* downloadScreenCache_ = nullptr;
    bool downloadAnimationActive_ = false;
    SupplyRunDisplay supplyRunDisplay_;
    bool operationLayoutTransition_ = false;
    bool operationScreenActive_ = false;
    std::string operationTitle_, operationDetail_, operationCancelLabel_;
    int operationProgress_ = -1;
    bool operationCancellable_ = false;
    std::uint32_t downloadAnimationLastDraw_ = 0;
    // Licensed Lucide PNGs, loaded once and tinted. Supersampled procedural
    // fallbacks keep navigation usable if an optional asset is unavailable.
    std::array<SDL_Texture*,11> navigationIcons_{};
    std::unordered_map<std::string,SDL_Texture*> pluginArtwork_;
    std::unordered_map<std::string,SDL_Texture*> newsImages_;
    std::unordered_map<std::string,CachedText> newsTextCache_;
    std::unordered_map<std::string,CachedText> textCache_;
    std::unordered_map<std::string,std::pair<int,int>> textMeasureCache_;
    std::unordered_map<std::string,FlowDocument> flowDocuments_;
    std::vector<NewsBlock> newsBlocks_;
    std::string preparedNewsSource_;
    std::string preparedNewsPreview_;
    int newsMaximumScroll_ = 0;
    int newsPageBottom_ = 670;
    std::vector<SupportBlock> supportBlocks_;
    std::string preparedSupportSource_;
    int supportMaximumScroll_ = 0;
    int localModDetailsMaximumScroll_ = 0;
    int pluginCatalogMaximumScroll_ = 0;
    int pluginDetailsMaximumScroll_ = 0;
    int navigationSelected_ = 0;
    bool navigationFocused_ = false;
    TapGesture tapGesture_;
    std::vector<TouchTarget> touchTargets_;
    std::vector<TouchTarget> pendingTouchTargets_;
    std::uint64_t touchRevision_ = 1;
    std::string touchIdentity_;
    std::string pendingTouchIdentity_;
    std::uint32_t suppressedTouchButtons_ = 0;
    // SDL_ttf otherwise keeps the WUHB-backed font files open for the lifetime
    // of each TTF_Font. Memory-backed fonts let the self-updater safely unmount
    // the running WUHB immediately before replacing it.
    std::vector<unsigned char> regularFontData_;
    std::vector<unsigned char> boldFontData_;
    TTF_Font* regular18_ = nullptr;
    TTF_Font* regular22_ = nullptr;
    TTF_Font* bold20_ = nullptr;
    TTF_Font* bold28_ = nullptr;
    TTF_Font* bold38_ = nullptr;

    void backdrop();
    void drawLoading(const std::string& title, const std::string& detail, int progress,
                     bool cancellable, const char* cancelLabel, bool miniGame);
    void loadWaitingGame();
    void drawWaitingGame(std::uint32_t now);
    void renderWaitingGameFrame(std::uint32_t now);
    void beginTouch(const std::string& identity);
    void touchTarget(SDL_Rect rect, TouchActionKind kind, int value,
                     const SDL_Rect* clip = nullptr);
    void header(const std::string& section, bool gameSwitchEnabled=false, int gameChoice=-1);
    void navigationRail(int activeSection, bool interactive = true);
    void installSteps(int current);
    void directionIcon(SDL_Rect rect, std::uint32_t direction, SDL_Color color);
    void checkmark(SDL_Rect rect, SDL_Color color);
    void primaryAction(SDL_Rect rect, const std::string& label, bool focused, bool enabled = true);
    void artworkCover(SDL_Texture* texture, SDL_Rect rect);
    void footer(const std::vector<std::pair<std::string,std::string>>& actions,
                bool subdued = true);
    void text(TTF_Font* font, const std::string& value, int x, int y, SDL_Color color,
              int maxWidth = 0, bool centered = false);
    void wrapped(TTF_Font* font, const std::string& value, SDL_Rect box, SDL_Color color);
    const CachedText* wrappedText(TTF_Font* font, const std::string& value, int width,
                                 SDL_Color color);
    int wrappedScrollable(TTF_Font* font, const std::string& value, SDL_Rect box,
                          SDL_Color color, int scrollOffset);
    FlowDocument& flowDocument(TTF_Font* font, const std::string& value, int width,
                               SDL_Color color);
    int flowText(TTF_Font* font, const std::string& value, int x, int y, int width,
                 SDL_Color color);
    void scrollBar(SDL_Rect viewport, int offset, int maximum);
    void quietPanel(SDL_Rect rect, bool focused = false);
    std::pair<int,int> measure(TTF_Font* font, const std::string& value);
    void roundRect(SDL_Rect rect, int radius, SDL_Color color);
    void outline(SDL_Rect rect, int radius, SDL_Color color, int thickness = 3);
    void circle(int x, int y, int radius, SDL_Color color);
    void present();
    void pill(const std::string& value, int x, int y, SDL_Color fill);
    SDL_Texture* pluginArtwork(const WorkshopPlugin& plugin) const;
    void workshopHeader(WorkshopTab tab);
    CachedText richTexture(TTF_Font* font, const std::string& value, int width,
                           SDL_Color color);
    void prepareSupport(const std::string& report);
    void actionCard(SDL_Rect rect, const std::string& eyebrow, const std::string& title,
                    const std::string& detail, bool selected, SDL_Color accent, bool enabled = true);
};

} // namespace unbound
