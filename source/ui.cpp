#include "ui.hpp"
#include "config.hpp"
#include "input.hpp"
#include "plugin_policy.hpp"
#include "install_feedback.hpp"
#include "supply_run_art.hpp"
#include "supply_run_extra_art.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <iomanip>


namespace unbound {
namespace {
constexpr int W=1280,H=720;
constexpr SDL_Color White{255,255,255,255},Ink{25,30,34,255},Muted{151,161,167,255};
constexpr SDL_Color Blue{0,159,207,255},Pale{238,244,247,255};
SDL_Color accentColor(){return config::ghostsMode()?SDL_Color{224,225,220,255}:SDL_Color{255,104,12,255};}
SDL_Color darkColor(){return SDL_Color{12,15,17,255};}
SDL_Color panelColor(){return SDL_Color{19,23,26,255};}
SDL_Color raisedColor(){return SDL_Color{29,34,37,255};}
constexpr SDL_Color Green{62,193,113,255},Red{218,67,72,255};
constexpr SDL_Color Steam{12,15,17,255},SteamCard{27,31,34,255};
constexpr SDL_Color PanelBorder{77,84,89,255};
constexpr SDL_Color LightText{205,211,215,255};
constexpr int NewsClipTop=102,NewsClipBottom=658;
// Retain the originally centered clip; the optional left/right clips are no
// longer loaded or drawn. Keeping the generated index preserves its timing.
std::string displayLanguage(std::string language){
    if(!language.empty())language[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(language[0])));
    return language;
}
std::string displayPackageSize(std::uint64_t bytes){
    std::ostringstream value;
    const bool gib=bytes>=1024ULL*1024*1024;
    value<<std::fixed<<std::setprecision(gib?2:1)
         <<double(bytes)/(gib?1073741824.0:1048576.0)<<(gib?" GiB":" MiB");
    return value.str();
}
bool newsVisible(int y,int height){return y<NewsClipBottom&&y+height>NewsClipTop;}
const std::vector<std::vector<std::string>> KeyboardRows={
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L"},
    {"Z","X","C","V","B","N","M"},
    {"SPACE","BACKSPACE",".",",","!","?","POST","CANCEL"}
};
int rating(const WorkshopMod& mod){const unsigned total=mod.upvotes+mod.downvotes;return total?int((mod.upvotes*5+total/2)/total):0;}
std::string stars(const WorkshopMod& mod){std::string result;const int score=rating(mod);for(int i=0;i<5;++i)result+=i<score?"*":"-";return result;}
bool loadFontData(const char* path,std::vector<unsigned char>& data){
    constexpr Sint64 MaximumFontBytes=16*1024*1024;
    SDL_RWops* source=SDL_RWFromFile(path,"rb");if(!source)return false;
    const Sint64 length=SDL_RWsize(source);bool ok=length>0&&length<=MaximumFontBytes;
    if(ok){data.resize(static_cast<std::size_t>(length));ok=SDL_RWread(source,data.data(),1,data.size())==data.size();}
    SDL_RWclose(source);if(!ok)data.clear();return ok;
}
TTF_Font* openMemoryFont(const std::vector<unsigned char>& data,int points){
    SDL_RWops* source=SDL_RWFromConstMem(data.data(),static_cast<int>(data.size()));
    if(!source)return nullptr;
    TTF_Font* font=TTF_OpenFontRW(source,1,points);return font;
}
// Icons use a consistent 32-unit design grid. Supersample alpha once, then
// linearly sample the alpha silhouettes. Unlike integer SDL line primitives,
// the stroke weight and diagonals remain consistent at GamePad scale.
bool iconPolygon(double x,double y,std::initializer_list<std::pair<double,double>> vertices){
    bool inside=false;auto previous=*(vertices.end()-1);
    for(const auto& point:vertices){
        if((point.second>y)!=(previous.second>y)&&
           x<(previous.first-point.first)*(y-point.second)/(previous.second-point.second)+point.first)
            inside=!inside;
        previous=point;
    }
    return inside;
}
bool iconBox(double x,double y,double left,double top,double right,double bottom){
    return x>=left&&x<=right&&y>=top&&y<=bottom;
}
bool iconCircle(double x,double y,double cx,double cy,double radius){
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
}
bool iconShape(int icon,double x,double y){
    if(icon==10){
        const double dx=x-16,dy=y-16;
        const bool disk=dx*dx+dy*dy<=169;
        const double ellipse=dx*dx/49+dy*dy/169;
        return disk&&(dx*dx+dy*dy>=121||std::abs(dy)<1.1||std::abs(ellipse-1)<.17);
    }
    if(icon==9)return iconPolygon(x,y,{{7,3},{29,16},{7,29}});
    if(icon>=5){
        double ax=x,ay=y;
        if(icon==6)ay=32-y;
        else if(icon==7){ax=32-y;ay=x;}
        else if(icon==8){ax=y;ay=32-x;}
        return iconBox(ax,ay,14,10,18,29)||
               iconPolygon(ax,ay,{{3,14},{16,2},{29,14},{25,18},{16,10},{7,18}});
    }
    if(icon==0){
        const bool roof=iconPolygon(x,y,{{2,15},{16,3},{30,15},{27,18},{16,8},{5,18}});
        const bool house=iconBox(x,y,7,13,25,29)&&!iconBox(x,y,13,21,19,30);
        return roof||house;
    }
    if(icon==1){
        const bool arrow=iconBox(x,y,14,3,18,18)||
            iconPolygon(x,y,{{6,14},{10,11},{16,18},{22,11},{26,14},{16,25}});
        const bool tray=iconBox(x,y,4,25,28,29)||iconBox(x,y,4,21,8,29)||iconBox(x,y,24,21,28,29);
        return arrow||tray;
    }
    if(icon==2){
        return iconBox(x,y,4,4,14,14)||iconBox(x,y,18,4,28,14)||
               iconBox(x,y,4,18,14,28)||iconBox(x,y,18,18,28,28);
    }
    if(icon==3){
        return iconPolygon(x,y,{{3,7},{13,7},{17,11},{29,11},{29,28},{3,28}});
    }
    const bool disk=iconCircle(x,y,16,16,13);
    const bool question=(iconCircle(x,y,16,12,5)&&!iconCircle(x,y,16,12,2.5)&&y<14)||
        iconPolygon(x,y,{{18,11},{21,13},{18,17},{17.5,20},{14,20},{14,16},{17,13}})||
        iconCircle(x,y,16,24,1.7);
    return disk&&!question;
}
SDL_Texture* createNavigationIcon(SDL_Renderer* renderer,int icon){
    // Official Lucide geometry is rasterized offline at 128 px, then tinted
    // and sampled proportionally. See content/licenses/Lucide-LICENSE.txt.
    constexpr std::array<const char*,10> names{{"house","download","layout-grid","folder",
        "circle-question-mark","arrow-up","arrow-down","arrow-left","arrow-right","play"}};
    if(icon>=0&&static_cast<std::size_t>(icon)<names.size()){
        const std::string path=std::string("fs:/vol/content/assets/icons/")+names[icon]+".png";
        if(SDL_Texture* texture=IMG_LoadTexture(renderer,path.c_str())){
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
            return texture;
        }
    }
    // An optional artwork problem must not prevent navigation or startup.
    constexpr int size=64,samples=4;
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,size,size,32,SDL_PIXELFORMAT_RGBA32);
    if(!surface)return nullptr;
    if(SDL_LockSurface(surface)!=0){SDL_FreeSurface(surface);return nullptr;}
    for(int y=0;y<size;++y){
        auto* row=reinterpret_cast<Uint32*>(static_cast<unsigned char*>(surface->pixels)+y*surface->pitch);
        for(int x=0;x<size;++x){
            unsigned covered=0;
            for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx)
                covered+=iconShape(icon,(x+(sx+0.5)/samples)/2.0,(y+(sy+0.5)/samples)/2.0)?1U:0U;
            row[x]=SDL_MapRGBA(surface->format,255,255,255,static_cast<Uint8>(covered*255/(samples*samples)));
        }
    }
    SDL_UnlockSurface(surface);
    SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_FreeSurface(surface);
    if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    return texture;
}
}

bool Ui::initialize(std::string& error){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){error=SDL_GetError();return false;}
    if((IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG)==0){error=IMG_GetError();return false;}
    if(TTF_Init()!=0){error=TTF_GetError();return false;}
    window_=SDL_CreateWindow(config::AppName,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,SDL_WINDOW_SHOWN);
    if(!window_){error=SDL_GetError();return false;}
    renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer_)renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer_){error=SDL_GetError();return false;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    SDL_RenderSetLogicalSize(renderer_,W,H);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    touchTargets_.reserve(48);pendingTouchTargets_.reserve(48);
    const char* regular="fs:/vol/content/fonts/NotoSans-Regular.ttf";const char* bold="fs:/vol/content/fonts/NotoSans-Bold.ttf";
    if(!loadFontData(regular,regularFontData_)||!loadFontData(bold,boldFontData_)){error=SDL_GetError();return false;}
    regular18_=openMemoryFont(regularFontData_,18);regular22_=openMemoryFont(regularFontData_,22);bold20_=openMemoryFont(boldFontData_,20);bold28_=openMemoryFont(boldFontData_,28);bold38_=openMemoryFont(boldFontData_,38);
    if(!regular18_||!regular22_||!bold20_||!bold28_||!bold38_){error=TTF_GetError();return false;}
    boot_=IMG_LoadTexture(renderer_,"fs:/vol/content/assets/Boot_Screen.png");logo_=IMG_LoadTexture(renderer_,"fs:/vol/content/assets/unbound_short.png");squareLogo_=IMG_LoadTexture(renderer_,"fs:/vol/content/assets/fixed_logo.png");
    if(!boot_||!logo_||!squareLogo_){error=IMG_GetError();return false;}
    for(std::size_t i=0;i<navigationIcons_.size();++i){
        navigationIcons_[i]=createNavigationIcon(renderer_,static_cast<int>(i));
        if(!navigationIcons_[i]){error=SDL_GetError();return false;}
    }
    if(SDL_Texture* artwork=IMG_LoadTexture(renderer_,"fs:/vol/content/assets/gamepad_mic_redirect.png")){
        SDL_SetTextureBlendMode(artwork,SDL_BLENDMODE_BLEND);
        pluginArtwork_["GAMEPAD_MIC_REDIRECT"]=artwork;
    }
    if(SDL_Texture* artwork=IMG_LoadTexture(renderer_,"fs:/vol/content/assets/protarium_network.png")){
        SDL_SetTextureBlendMode(artwork,SDL_BLENDMODE_BLEND);
        pluginArtwork_["PROTARIUM"]=artwork;
    }
    // Audio is optional and memory-backed. It keeps playing during network and
    // install workers without any music-file reads or UI-frame polling.
    backgroundMusic_.start("fs:/vol/content/audio/hq_theme.wav");
    loadWaitingGame();
    return true;
}
void Ui::shutdown(){
    networkGlobe_.clear();
    backgroundMusic_.stop();
    downloadAnimationActive_=false;
    if(waitingAtlas_)SDL_DestroyTexture(waitingAtlas_);
    waitingAtlas_=nullptr;
    if(downloadScreenCache_)SDL_DestroyTexture(downloadScreenCache_);
    downloadScreenCache_=nullptr;
    newsHero_=nullptr;
    for(auto& texture:navigationIcons_){if(texture)SDL_DestroyTexture(texture);texture=nullptr;}
    for(auto& document:flowDocuments_)for(auto& chunk:document.second.chunks)if(chunk.text.texture)SDL_DestroyTexture(chunk.text.texture);
    flowDocuments_.clear();
    for(auto& item:textCache_)if(item.second.texture)SDL_DestroyTexture(item.second.texture);
    textCache_.clear();textMeasureCache_.clear();
    for(auto& item:newsTextCache_)if(item.second.texture)SDL_DestroyTexture(item.second.texture);
    newsTextCache_.clear();newsBlocks_.clear();
    for(auto& item:supportBlocks_)if(item.text.texture)SDL_DestroyTexture(item.text.texture);
    supportBlocks_.clear();preparedSupportSource_.clear();supportMaximumScroll_=0;
    for(auto& item:newsImages_)if(item.second)SDL_DestroyTexture(item.second);
    newsImages_.clear();
    for(auto& item:pluginArtwork_)if(item.second)SDL_DestroyTexture(item.second);
    pluginArtwork_.clear();
    for(auto* t:{boot_,logo_,squareLogo_})if(t)SDL_DestroyTexture(t);
    for(auto* f:{regular18_,regular22_,bold20_,bold28_,bold38_})if(f)TTF_CloseFont(f);
    boot_=logo_=squareLogo_=nullptr;regular18_=regular22_=bold20_=bold28_=bold38_=nullptr;
    regularFontData_.clear();boldFontData_.clear();
    if(renderer_)SDL_DestroyRenderer(renderer_);
    if(window_)SDL_DestroyWindow(window_);
    renderer_=nullptr;window_=nullptr;TTF_Quit();IMG_Quit();SDL_Quit();
}
void Ui::setNewsImages(const std::vector<NewsImage>& images){
    newsHero_=nullptr;
    for(auto& item:newsTextCache_)if(item.second.texture)SDL_DestroyTexture(item.second.texture);
    newsTextCache_.clear();newsBlocks_.clear();preparedNewsSource_.clear();
    for(auto& item:newsImages_)if(item.second)SDL_DestroyTexture(item.second);
    newsImages_.clear();
    constexpr int maxWidth=940,maxHeight=310;
    for(const auto& image:images){
        if(image.cachePath.empty())continue;
        const std::string scaledPath=image.cachePath+".bmp";SDL_Surface* source=nullptr;SDL_Surface* prepared=SDL_LoadBMP(scaledPath.c_str());
        if(prepared&&(prepared->w<=0||prepared->h<=0||prepared->w>maxWidth||prepared->h>maxHeight)){SDL_FreeSurface(prepared);prepared=nullptr;}
        if(!prepared){
            source=IMG_Load(image.cachePath.c_str());if(!source)continue;
            const double scale=std::min({1.0,double(maxWidth)/std::max(1,source->w),double(maxHeight)/std::max(1,source->h)});const int width=std::max(1,int(source->w*scale)),height=std::max(1,int(source->h*scale));prepared=source;
            if(width!=source->w||height!=source->h){prepared=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);if(prepared){SDL_FillRect(prepared,nullptr,SDL_MapRGBA(prepared->format,0,0,0,0));SDL_Rect destination{0,0,width,height};if(SDL_BlitScaled(source,nullptr,prepared,&destination)!=0){SDL_FreeSurface(prepared);prepared=nullptr;}}}
            if(prepared)SDL_SaveBMP(prepared,scaledPath.c_str());
        }
        if(prepared){if(SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer_,prepared)){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);newsImages_[image.url]=texture;if(!newsHero_)newsHero_=texture;}}
        if(prepared&&prepared!=source)SDL_FreeSurface(prepared);
        if(source)SDL_FreeSurface(source);
    }
}
void Ui::pump(){
    // A screen without explicit touch registration publishes an inert map,
    // never the previous screen's controls.
    pendingTouchTargets_.clear();pendingTouchIdentity_="inactive";suppressedTouchButtons_=0;
    SDL_Event event{};while(SDL_PollEvent(&event)){};
}
void Ui::circle(int x,int y,int radius,SDL_Color color){SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);for(int dy=-radius;dy<=radius;++dy){const int dx=int(std::sqrt(double(radius*radius-dy*dy)));SDL_RenderDrawLine(renderer_,x-dx,y+dy,x+dx,y+dy);}}
void Ui::present(){
    if(pendingTouchIdentity_!=touchIdentity_||pendingTouchTargets_!=touchTargets_){
        touchIdentity_=pendingTouchIdentity_;touchTargets_=pendingTouchTargets_;++touchRevision_;
        // A page can change during a synchronous rescan/install that does not
        // poll touch. Require a release even if that held press was never seen.
        tapGesture_.reset();
    }
    SDL_RenderPresent(renderer_);
    if(textCache_.size()>256){for(auto& item:textCache_)if(item.second.texture)SDL_DestroyTexture(item.second.texture);textCache_.clear();}
    if(textMeasureCache_.size()>256)textMeasureCache_.clear();
}
void Ui::beginTouch(const std::string& identity){
    if(identity!="loading-cancel"&&identity!="loading-safe")downloadAnimationActive_=false;
    if(identity!="loading-cancel"&&identity!="loading-safe"&&identity!="loading-normal")operationScreenActive_=false;
    pendingTouchIdentity_=identity;pendingTouchTargets_.clear();suppressedTouchButtons_=0;
}
void Ui::touchTarget(SDL_Rect rect,TouchActionKind kind,int value,const SDL_Rect* clip){
    if(clip){SDL_Rect intersection{};if(!SDL_IntersectRect(&rect,clip,&intersection))return;rect=intersection;}
    if(rect.w<=0||rect.h<=0)return;
    pendingTouchTargets_.push_back({{rect.x,rect.y,rect.w,rect.h},kind,value});
}
std::optional<TouchAction> Ui::touchAction(const TouchSample& sample){
    return tapGesture_.update(sample,touchTargets_,touchRevision_);
}
void Ui::invalidateTouch(){
    downloadAnimationActive_=false;
    operationScreenActive_=false;
    tapGesture_.reset();touchTargets_.clear();pendingTouchTargets_.clear();
    touchIdentity_.clear();pendingTouchIdentity_.clear();++touchRevision_;
}
void Ui::cancelTouchGesture(){tapGesture_.reset();}
void Ui::roundRect(SDL_Rect r,int radius,SDL_Color color){
    if(r.w<=0||r.h<=0)return;
    radius=std::min({radius,r.w/2,r.h/2,64});SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
    if(radius<=0){SDL_RenderFillRect(renderer_,&r);return;}
    std::array<SDL_Rect,129> spans{};int count=0;spans[count++]={r.x,r.y+radius,r.w,r.h-radius*2};
    for(int row=0;row<radius;++row){const int topDy=row-radius;const int topDx=int(std::sqrt(double(radius*radius-topDy*topDy)));spans[count++]={r.x+radius-topDx,r.y+row,r.w-radius*2+topDx*2,1};const int bottomDy=row+1;const int bottomDx=int(std::sqrt(double(radius*radius-bottomDy*bottomDy)));spans[count++]={r.x+radius-bottomDx,r.y+r.h-radius+row,r.w-radius*2+bottomDx*2,1};}
    SDL_RenderFillRects(renderer_,spans.data(),count);
}
void Ui::outline(SDL_Rect r,int radius,SDL_Color color,int thickness){roundRect(r,radius,color);SDL_Rect inner{r.x+thickness,r.y+thickness,r.w-thickness*2,r.h-thickness*2};roundRect(inner,std::max(0,radius-thickness),White);}
void Ui::text(TTF_Font* font,const std::string& value,int x,int y,SDL_Color color,int maxWidth,bool centered){if(value.empty())return;const std::string key="T|"+std::to_string(reinterpret_cast<std::uintptr_t>(font))+"|"+std::to_string(maxWidth)+"|"+std::to_string(color.r)+","+std::to_string(color.g)+","+std::to_string(color.b)+"|"+value;auto found=textCache_.find(key);if(found==textCache_.end()){std::string shown=value;int width=0,height=0;TTF_SizeUTF8(font,shown.c_str(),&width,&height);while(maxWidth>0&&width>maxWidth&&shown.size()>4){shown.pop_back();TTF_SizeUTF8(font,(shown+"...").c_str(),&width,&height);}if(shown!=value)shown+="...";SDL_Surface* surface=TTF_RenderUTF8_Blended(font,shown.c_str(),color);if(!surface)return;SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer_,surface);CachedText cached{texture,surface->w,surface->h};SDL_FreeSurface(surface);if(!texture)return;SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);found=textCache_.emplace(key,cached).first;}const auto& cached=found->second;SDL_Rect target{x-(centered?cached.width/2:0),y,cached.width,cached.height};SDL_RenderCopy(renderer_,cached.texture,nullptr,&target);}
const Ui::CachedText* Ui::wrappedText(TTF_Font* font,const std::string& value,int width,SDL_Color color){
    if(value.empty())return nullptr;
    const std::string key="W|"+std::to_string(reinterpret_cast<std::uintptr_t>(font))+"|"+
        std::to_string(width)+"|"+std::to_string(color.r)+","+std::to_string(color.g)+","+
        std::to_string(color.b)+"|"+value;
    auto found=textCache_.find(key);
    if(found==textCache_.end()){
        SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(font,value.c_str(),color,width);
        if(!surface)return nullptr;
        SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer_,surface);
        const CachedText cached{texture,surface->w,surface->h};SDL_FreeSurface(surface);
        if(!texture)return nullptr;
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        found=textCache_.emplace(key,cached).first;
    }
    return &found->second;
}
void Ui::wrapped(TTF_Font* font,const std::string& value,SDL_Rect box,SDL_Color color){
    if(const CachedText* cached=wrappedText(font,value,box.w,color)){
        SDL_Rect target{box.x,box.y,std::min(box.w,cached->width),std::min(box.h,cached->height)},
                 source{0,0,target.w,target.h};
        SDL_RenderCopy(renderer_,cached->texture,&source,&target);
    }
}
int Ui::wrappedScrollable(TTF_Font* font,const std::string& value,SDL_Rect box,
                          SDL_Color color,int scrollOffset){
    const int maximum=std::max(0,flowDocument(font,value,box.w,color).height-box.h);
    const int offset=std::clamp(scrollOffset,0,maximum);
    SDL_Rect previous{};SDL_RenderGetClipRect(renderer_,&previous);
    const bool previousEnabled=SDL_RenderIsClipEnabled(renderer_);
    SDL_RenderSetClipRect(renderer_,&box);
    flowText(font,value,box.x,box.y-offset,box.w,color);
    SDL_RenderSetClipRect(renderer_,previousEnabled?&previous:nullptr);
    return maximum;
}
std::pair<int,int> Ui::measure(TTF_Font* font,const std::string& value){const std::string key=std::to_string(reinterpret_cast<std::uintptr_t>(font))+"|"+value;const auto found=textMeasureCache_.find(key);if(found!=textMeasureCache_.end())return found->second;int width=0,height=0;TTF_SizeUTF8(font,value.c_str(),&width,&height);return textMeasureCache_.emplace(key,std::make_pair(width,height)).first->second;}
Ui::FlowDocument& Ui::flowDocument(TTF_Font* font,const std::string& value,int width,
                                   SDL_Color color){
    const std::string key=std::to_string(reinterpret_cast<std::uintptr_t>(font))+"|"+
        std::to_string(width)+"|"+std::to_string(color.r)+","+std::to_string(color.g)+","+
        std::to_string(color.b)+"|"+value;
    const auto found=flowDocuments_.find(key);if(found!=flowDocuments_.end())return found->second;
    // Layout is cached, and raster chunks are bounded. Very long descriptions
    // must not become a single texture larger than the Wii U renderer supports.
    if(flowDocuments_.size()>=24){
        const auto old=flowDocuments_.begin();
        for(auto& chunk:old->second.chunks)if(chunk.text.texture)SDL_DestroyTexture(chunk.text.texture);
        flowDocuments_.erase(old);
    }
    FlowDocument document;std::string chunk;unsigned count=0;
    const auto flush=[&](){
        if(chunk.empty())return;
        if(chunk.back()=='\n')chunk.pop_back();
        SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(font,chunk.c_str(),color,width);
        if(surface){
            document.chunks.push_back({chunk,{nullptr,surface->w,surface->h},document.height});
            document.height+=surface->h+std::max(0,TTF_FontLineSkip(font)-TTF_FontHeight(font));
            SDL_FreeSurface(surface);
        }
        chunk.clear();count=0;
    };
    const auto append=[&](const std::string& line){
        if(count>=12||chunk.size()+line.size()>1536)flush();
        chunk+=line;chunk.push_back('\n');++count;
    };
    std::istringstream lines(value);std::string line;
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()){append(" ");continue;}
        std::size_t start=0;
        while(start<line.size()){
            std::vector<std::size_t> boundaries;
            const std::size_t limit=std::min(line.size(),start+1536);
            for(std::size_t end=start+1;end<=limit;++end)
                if(end==line.size()||(static_cast<unsigned char>(line[end])&0xc0U)!=0x80U)
                    boundaries.push_back(end);
            if(boundaries.empty())boundaries.push_back(limit);
            std::size_t low=0,high=boundaries.size();
            while(low<high){
                const auto middle=low+(high-low)/2;int pixels=0,height=0;
                const auto prefix=line.substr(start,boundaries[middle]-start);
                TTF_SizeUTF8(font,prefix.c_str(),&pixels,&height);
                // SDL_ttf's wrapped glyph advances can exceed SizeUTF8's
                // bounding width slightly. Leave a small inset so an already
                // wrapped final word is not wrapped again onto an orphan line.
                if(pixels<=std::max(1,width-16))low=middle+1;else high=middle;
            }
            std::size_t end=boundaries[low?low-1:0];
            if(end<line.size()){
                const auto space=line.find_last_of(" \t",end-1);
                if(space!=std::string::npos&&space>start)end=space;
            }
            append(line.substr(start,end-start));start=end;
            while(start<line.size()&&(line[start]==' '||line[start]=='\t'))++start;
        }
    }
    flush();return flowDocuments_.emplace(key,std::move(document)).first->second;
}
int Ui::flowText(TTF_Font* font,const std::string& value,int x,int y,int width,
                  SDL_Color color){
    auto& document=flowDocument(font,value,width,color);
    SDL_Rect clip{};SDL_RenderGetClipRect(renderer_,&clip);
    if(!SDL_RenderIsClipEnabled(renderer_))clip={0,102,W,556};
    for(auto& chunk:document.chunks){
        SDL_Rect destination{x,y+chunk.y,chunk.text.width,chunk.text.height};
        if(destination.y+destination.h<=clip.y||destination.y>=clip.y+clip.h){
            // Retain layout, not offscreen raster memory. Entering a new page
            // cannot keep megabytes of unrelated long descriptions resident.
            if(chunk.text.texture){SDL_DestroyTexture(chunk.text.texture);chunk.text.texture=nullptr;}
            continue;
        }
        if(!chunk.text.texture){
            SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(font,chunk.source.c_str(),color,width);
            if(surface){chunk.text.texture=SDL_CreateTextureFromSurface(renderer_,surface);SDL_FreeSurface(surface);}
            if(chunk.text.texture)SDL_SetTextureBlendMode(chunk.text.texture,SDL_BLENDMODE_BLEND);
        }
        if(chunk.text.texture)SDL_RenderCopy(renderer_,chunk.text.texture,nullptr,&destination);
    }
    return document.height;
}
void Ui::quietPanel(SDL_Rect rect,bool focused){
    roundRect(rect,8,focused?accentColor():SDL_Color{43,49,53,255});
    roundRect({rect.x+1,rect.y+1,rect.w-2,rect.h-2},7,focused?raisedColor():panelColor());
    if(focused)roundRect({rect.x,rect.y+10,3,rect.h-20},1,accentColor());
}
void Ui::scrollBar(SDL_Rect viewport,int offset,int maximum){
    if(maximum<=0)return;
    const int thumbHeight=std::max(24,viewport.h*viewport.h/(viewport.h+maximum));
    roundRect({viewport.x+viewport.w+12,viewport.y,4,viewport.h},2,raisedColor());
    roundRect({viewport.x+viewport.w+12,viewport.y+(viewport.h-thumbHeight)*
        std::clamp(offset,0,maximum)/maximum,4,thumbHeight},2,Muted);
}
Ui::CachedText Ui::richTexture(TTF_Font* font,const std::string& value,int width,SDL_Color color){
    const std::string key=std::to_string(reinterpret_cast<std::uintptr_t>(font))+"|"+std::to_string(width)+"|"+std::to_string(color.r)+","+std::to_string(color.g)+","+std::to_string(color.b)+"|"+value;
    const auto existing=newsTextCache_.find(key);if(existing!=newsTextCache_.end())return existing->second;
    const int lineHeight=TTF_FontHeight(font)+7;
    struct Run{std::string value;bool bold;bool italic;};std::vector<Run> runs;std::string current;bool bold=false,italic=false;
    auto flush=[&](){if(!current.empty()){runs.push_back({current,bold,italic});current.clear();}};
    for(std::size_t i=0;i<value.size();){if(i+1<value.size()&&value[i]=='*'&&value[i+1]=='*'){flush();bold=!bold;i+=2;}else if(value[i]=='*'){flush();italic=!italic;++i;}else current.push_back(value[i++]);}flush();
    struct Word{std::string value;int x;int y;int style;};std::vector<Word> layout;
    const int originalStyle=TTF_GetFontStyle(font);int activeStyle=originalStyle;
    const auto useStyle=[&](int style){if(activeStyle!=style){TTF_SetFontStyle(font,style);activeStyle=style;}};
    int cursorX=0,cursorY=0,usedWidth=0;bool needsSpace=false;
    for(const auto& run:runs){const int style=(run.bold?TTF_STYLE_BOLD:TTF_STYLE_NORMAL)|(run.italic?TTF_STYLE_ITALIC:TTF_STYLE_NORMAL);useStyle(style);int spaceW=0,spaceH=0;TTF_SizeUTF8(font," ",&spaceW,&spaceH);std::istringstream words(run.value);std::string word;while(words>>word){int wordW=0,wordH=0;TTF_SizeUTF8(font,word.c_str(),&wordW,&wordH);const int added=(needsSpace?spaceW:0)+wordW;if(cursorX+added>width){cursorX=0;cursorY+=lineHeight;needsSpace=false;}if(needsSpace)cursorX+=spaceW;layout.push_back({word,cursorX,cursorY,style});cursorX+=wordW;usedWidth=std::max(usedWidth,cursorX);needsSpace=true;}}
    struct Segment{std::string value;int x;int y;int style;};std::vector<Segment> segments;
    for(const auto& word:layout){if(!segments.empty()&&segments.back().y==word.y&&segments.back().style==word.style)segments.back().value+=" "+word.value;else segments.push_back({word.value,word.x,word.y,word.style});}
    useStyle(originalStyle);const int totalHeight=cursorY+lineHeight,textureWidth=std::max(1,std::min(width,usedWidth+2));SDL_Texture* texture=nullptr;
    SDL_Surface* composed=SDL_CreateRGBSurfaceWithFormat(0,textureWidth,totalHeight,32,SDL_PIXELFORMAT_RGBA32);
    if(composed){SDL_FillRect(composed,nullptr,SDL_MapRGBA(composed->format,0,0,0,0));for(const auto& item:segments){useStyle(item.style);SDL_Surface* segment=TTF_RenderUTF8_Blended(font,item.value.c_str(),color);if(segment){SDL_SetSurfaceBlendMode(segment,SDL_BLENDMODE_NONE);SDL_Rect destination{item.x,item.y,segment->w,segment->h};SDL_BlitSurface(segment,nullptr,composed,&destination);SDL_FreeSurface(segment);}}useStyle(originalStyle);texture=SDL_CreateTextureFromSurface(renderer_,composed);SDL_FreeSurface(composed);}CachedText cached{texture,textureWidth,totalHeight};if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);newsTextCache_[key]=cached;}return cached;
}

void Ui::prepareNews(const std::string& changes){
    for(auto& item:newsTextCache_)if(item.second.texture)SDL_DestroyTexture(item.second.texture);
    newsTextCache_.clear();newsBlocks_.clear();preparedNewsSource_=changes;
    preparedNewsPreview_=plainNewsPreview(changes.substr(0,std::min<std::size_t>(changes.size(),64*1024)));
    if(preparedNewsPreview_.size()>360)preparedNewsPreview_.resize(360);
    auto addTexture=[this](const CachedText& cached,int x,int y){if(cached.texture)newsBlocks_.push_back({cached.texture,{x,y,cached.width,cached.height},0});};
    const auto badge=richTexture(bold20_,"WHAT'S NEW",180,accentColor());const int badgeWidth=measure(bold20_,"WHAT'S NEW").first+28;newsBlocks_.push_back({nullptr,{254,128,badgeWidth,36},3});addTexture(badge,268,132);
    int cursor=178;std::istringstream stream(changes);std::string line;
    std::size_t blockCount=0;
    while(blockCount<256&&cursor<16000&&std::getline(stream,line)){
        if(line.size()>4096)line.resize(4096);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const std::string clean=trim(line);
        if(clean.empty()){cursor+=12;continue;}
        const auto imageRefs=parseNewsImages(clean);
        if(!imageRefs.empty()){
            const auto& ref=imageRefs.front();const auto found=newsImages_.find(ref.url);
            if(found!=newsImages_.end()){int width=0,height=0;SDL_QueryTexture(found->second,nullptr,nullptr,&width,&height);if(width>0&&height>0){newsBlocks_.push_back({found->second,{268,cursor,width,height},0});cursor+=height+8;}}
            else{newsBlocks_.push_back({nullptr,{268,cursor,960,90},2});addTexture(richTexture(bold20_,"IMAGE UNAVAILABLE",400,accentColor()),288,cursor+18);addTexture(richTexture(regular18_,ref.url,920,Muted),288,cursor+49);cursor+=98;}
            if(!ref.caption.empty()){const auto caption=richTexture(regular18_,ref.caption,960,Muted);addTexture(caption,268,cursor);cursor+=caption.height+5;}cursor+=10;++blockCount;continue;
        }
        if(clean=="---"){newsBlocks_.push_back({nullptr,{268,cursor+6,960,1},1});cursor+=22;++blockCount;continue;}
        TTF_Font* font=regular18_;SDL_Color color=LightText;int x=268,width=960,spacing=5;std::string content=clean;
        if(clean.rfind("### ",0)==0){font=bold20_;color=accentColor();content=clean.substr(4);spacing=5;}
        else if(clean.rfind("## ",0)==0){font=bold20_;color=White;content=clean.substr(3);spacing=7;}
        else if(clean.rfind("# ",0)==0){font=bold28_;color=White;content=clean.substr(2);spacing=9;}
        else if(clean.rfind("- ",0)==0){const auto bullet=richTexture(bold20_,"•",24,accentColor());addTexture(bullet,270,cursor);x=298;width=930;content=clean.substr(2);spacing=3;}
        const auto rendered=richTexture(font,content,width,color);addTexture(rendered,x,cursor);cursor+=rendered.height+spacing;++blockCount;
    }
    newsPageBottom_=std::max(670,cursor+25);newsMaximumScroll_=std::max(0,cursor-630);
    header("What's new");footer({{"UP/DN","Scroll / swipe page"},{"B","Back"}});
}
void Ui::backdrop(){SDL_SetRenderDrawColor(renderer_,12,15,17,255);SDL_RenderClear(renderer_);}
void Ui::setNavigation(int selected,bool focused){navigationSelected_=std::clamp(selected,0,5);navigationFocused_=focused;}
void Ui::directionIcon(SDL_Rect rect,std::uint32_t direction,SDL_Color color){
    const std::size_t index=direction==button::Up?5:(direction==button::Down?6:
                            (direction==button::Left?7:8));
    SDL_Texture* icon=navigationIcons_[index];SDL_SetTextureColorMod(icon,color.r,color.g,color.b);
    SDL_RenderCopy(renderer_,icon,nullptr,&rect);
}
void Ui::checkmark(SDL_Rect rect,SDL_Color color){
    SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
    const int x=rect.x,y=rect.y;
    for(int stroke=0;stroke<2;++stroke){
        SDL_RenderDrawLine(renderer_,x+2,y+rect.h/2+stroke,x+rect.w/3,y+rect.h-3+stroke);
        SDL_RenderDrawLine(renderer_,x+rect.w/3,y+rect.h-3+stroke,x+rect.w-2,y+3+stroke);
    }
}
void Ui::primaryAction(SDL_Rect rect,const std::string& label,bool focused,bool enabled){
    if(focused)roundRect({rect.x-3,rect.y-3,rect.w+6,rect.h+6},10,White);
    roundRect(rect,7,enabled?accentColor():raisedColor());
    const auto foreground=enabled?(config::ghostsMode()?Ink:White):Muted;
    const int labelWidth=std::min(measure(bold20_,label).first,rect.w-68);
    const int groupWidth=labelWidth+40;
    const int x=rect.x+(rect.w-groupWidth)/2;
    circle(x+14,rect.y+rect.h/2,14,foreground);
    text(bold20_,"A",x+14,rect.y+(rect.h-28)/2,enabled?accentColor():raisedColor(),0,true);
    text(bold20_,label,x+40,rect.y+(rect.h-28)/2,foreground,rect.w-66);
}
void Ui::artworkCover(SDL_Texture* texture,SDL_Rect rect){
    int width=0,height=0;
    if(!texture||SDL_QueryTexture(texture,nullptr,nullptr,&width,&height)!=0||width<=0||height<=0)return;
    const double scale=std::max(double(rect.w)/width,double(rect.h)/height);
    const int cropWidth=std::min(width,int(std::ceil(rect.w/scale)));
    const int cropHeight=std::min(height,int(std::ceil(rect.h/scale)));
    const SDL_Rect source{(width-cropWidth)/2,(height-cropHeight)/6,cropWidth,cropHeight};
    SDL_RenderCopy(renderer_,texture,&source,&rect);
}
void Ui::navigationRail(int activeSection,bool interactive){
    const SDL_Rect rail{0,102,NavigationRailWidth,556};
    SDL_SetRenderDrawColor(renderer_,darkColor().r,darkColor().g,darkColor().b,255);
    SDL_RenderFillRect(renderer_,&rail);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,NavigationRailWidth,102,NavigationRailWidth,658);
    const std::array<std::string,6> labels{{"HQ","Install / Update","Workshop","Mod Manager","Support","Network"}};
    const int chosen=navigationFocused_?navigationSelected_:activeSection;
    for(int i=0;i<6;++i){
        const int y=142+i*66;
        const bool selected=chosen==i;
        const SDL_Rect row{14,y,198,52};
        if(interactive)touchTarget(row,TouchActionKind::Sidebar,i);
        if(selected){
            roundRect(row,7,raisedColor());
            roundRect({row.x,row.y,4,row.h},2,accentColor());
            if(navigationFocused_){
                SDL_SetRenderDrawColor(renderer_,accentColor().r,accentColor().g,accentColor().b,255);
                SDL_RenderDrawRect(renderer_,&row);
            }
        }
        const SDL_Color color=selected?White:Muted;
        text(regular18_,labels[i],61,y+13,color,144);
        auto* icon=navigationIcons_[static_cast<std::size_t>(i==5?10:i)];
        SDL_SetTextureColorMod(icon,color.r,color.g,color.b);
        const SDL_Rect iconRect{25,y+12,28,28};
        SDL_RenderCopy(renderer_,icon,nullptr,&iconRect);
    }
}
void Ui::header(const std::string&,bool gameSwitchEnabled,int gameChoice){
    const SDL_Rect bar{0,0,W,102};
    SDL_SetRenderDrawColor(renderer_,darkColor().r,darkColor().g,darkColor().b,255);
    SDL_RenderFillRect(renderer_,&bar);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,NavigationRailWidth,0,NavigationRailWidth,102);
    SDL_SetRenderDrawColor(renderer_,accentColor().r,accentColor().g,accentColor().b,255);
    SDL_RenderDrawLine(renderer_,NavigationRailWidth,100,W,100);
    int logoWidth=0,logoHeight=0;
    if(SDL_QueryTexture(logo_,nullptr,nullptr,&logoWidth,&logoHeight)==0&&logoWidth>0&&logoHeight>0){
        const float scale=std::min(190.0f/logoWidth,98.0f/logoHeight);
        const SDL_FRect mark{(NavigationRailWidth-logoWidth*scale)/2,(102-logoHeight*scale)/2,
                             logoWidth*scale,logoHeight*scale};
        SDL_RenderCopyF(renderer_,logo_,nullptr,&mark);
    }
    const std::string build=std::string(config::AppVersion)+" · r"+std::to_string(config::AppReleaseRevision);
    text(regular18_,build,W-28-measure(regular18_,build).first,39,Muted);

    const SDL_Rect control{ContentLeft,24,424,54};
    roundRect(control,8,PanelBorder);
    roundRect({control.x+1,control.y+1,control.w-2,control.h-2},7,darkColor());
    const int segmentWidth=control.w/2;
    const int active=config::ghostsMode()?1:0;
    const char* labels[]={"Black Ops II","Ghosts"};
    const SDL_Color activeFill=config::ghostsMode()?SDL_Color{55,58,59,255}:SDL_Color{58,36,23,255};
    for(int i=0;i<2;++i){
        const SDL_Rect segment{control.x+i*segmentWidth,control.y,segmentWidth,control.h};
        if(i==active){
            roundRect({segment.x+3,segment.y+3,segment.w-6,segment.h-6},6,activeFill);
            checkmark({segment.x+20,segment.y+18,17,17},accentColor());
        }
        text(bold20_,labels[i],segment.x+segment.w/2+(i==active?10:0),segment.y+12,
             i==active?accentColor():LightText,segment.w-48,true);
        if(i==gameChoice){
            SDL_SetRenderDrawColor(renderer_,White.r,White.g,White.b,255);
            const SDL_Rect focus{segment.x+2,segment.y+2,segment.w-4,segment.h-4};
            SDL_RenderDrawRect(renderer_,&focus);
        }
        if(gameSwitchEnabled)touchTarget(segment,TouchActionKind::GameChoice,i);
    }
}
void Ui::pill(const std::string& value,int x,int y,SDL_Color fill){const auto size=measure(regular18_,value);SDL_Rect r{x,y,size.first+22,30};roundRect(r,6,raisedColor());const SDL_Color color=fill.r==Blue.r&&fill.g==Blue.g?LightText:(fill.r==PanelBorder.r&&fill.g==PanelBorder.g?Muted:fill);text(regular18_,value,x+11,y+2,color);}
SDL_Texture* Ui::pluginArtwork(const WorkshopPlugin& plugin) const{
    const auto found=pluginArtwork_.find(plugin.key);
    return found!=pluginArtwork_.end()&&found->second?found->second:squareLogo_;
}
void Ui::footer(const std::vector<std::pair<std::string,std::string>>& actions,bool subdued){
    SDL_Rect bar{0,658,W,62};SDL_SetRenderDrawColor(renderer_,15,18,20,248);SDL_RenderFillRect(renderer_,&bar);
    if(subdued){SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,28,658,W-28,658);}
    if(actions.empty())return;
    const int columnWidth=W/static_cast<int>(actions.size());
    for(std::size_t i=0;i<actions.size();++i){
        const auto& action=actions[i];
        const bool horizontalDpad=action.first=="DPAD-LR";
        const bool verticalDpad=action.first=="UP/DN";
        const bool splitDpad=horizontalDpad||verticalDpad;
        int keyWidth=0,labelWidth=0;
        if(!splitDpad)keyWidth=measure(bold20_,action.first).first;
        labelWidth=measure(regular18_,action.second).first;
        const int badgeWidth=splitDpad?94:std::max(38,keyWidth+20);
        const int labelRoom=std::max(0,columnWidth-badgeWidth-34);
        const int contentWidth=badgeWidth+12+std::min(labelRoom,labelWidth);
        const int start=static_cast<int>(i)*columnWidth+std::max(14,(columnWidth-contentWidth)/2);
        SDL_Rect badge{start,672,badgeWidth,34};
        if(splitDpad){
            for(int direction=0;direction<2;++direction){
                SDL_Rect arrow{badge.x+direction*50,badge.y,44,badge.h};
                roundRect(arrow,7,PanelBorder);roundRect({arrow.x+1,arrow.y+1,42,arrow.h-2},6,raisedColor());
                directionIcon({arrow.x+11,arrow.y+6,22,22},
                    horizontalDpad?(direction==0?button::Left:button::Right):
                                   (direction==0?button::Up:button::Down),White);
                touchTarget({arrow.x,663,44,52},TouchActionKind::Button,
                    static_cast<int>(horizontalDpad?(direction==0?button::Left:button::Right):
                                     (direction==0?button::Up:button::Down)));
            }
        }else{
            if(subdued){roundRect(badge,7,PanelBorder);roundRect({badge.x+1,badge.y+1,badge.w-2,badge.h-2},6,raisedColor());}
            else roundRect(badge,10,accentColor());
            text(bold20_,action.first,badge.x+badge.w/2,badge.y+3,!subdued&&config::ghostsMode()?Ink:White,0,true);
            std::uint32_t bits=0;
            if(action.first=="A"||action.first=="A/UP")bits=button::A;
            else if(action.first=="B")bits=button::B;
            else if(action.first=="X/1"||action.first=="X")bits=button::X;
            else if(action.first=="Y/2"||action.first=="Y")bits=button::Y;
            else if(action.first=="+")bits=button::Plus;
            else if(action.first=="-")bits=button::Minus;
            else if(action.first=="L")bits=button::L;
            else if(action.first=="UP")bits=button::Up;
            else if(action.first=="DOWN")bits=button::Down;
            else if(action.first=="RIGHT")bits=button::Right;
            if(bits&&!(suppressedTouchButtons_&bits))
                touchTarget({static_cast<int>(i)*columnWidth+8,663,columnWidth-16,52},
                            TouchActionKind::Button,static_cast<int>(bits));
        }
        text(regular18_,action.second,badge.x+badge.w+12,676,subdued?LightText:White,columnWidth-badgeWidth-34);
    }
}
void Ui::bootScreen(){pump();beginTouch("boot");SDL_RenderCopy(renderer_,boot_,nullptr,nullptr);SDL_Rect shade{0,640,W,80};SDL_SetRenderDrawColor(renderer_,0,0,0,175);SDL_RenderFillRect(renderer_,&shade);text(regular22_,"Loading Unbound HQ...",W/2,663,White,0,true);present();}
void Ui::loadWaitingGame(){
    if(!SDL_RenderTargetSupported(renderer_))return;
    downloadScreenCache_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET,W,H);
    if(!downloadScreenCache_)return;
    SDL_SetTextureBlendMode(downloadScreenCache_,SDL_BLENDMODE_NONE);
    SDL_Surface* atlas=SDL_CreateRGBSurfaceWithFormat(0,512,112,32,SDL_PIXELFORMAT_RGBA32);
    if(!atlas)return;
    SDL_FillRect(atlas,nullptr,SDL_MapRGBA(atlas->format,0,0,0,0));
    const auto color=[](char pixel){
        switch(pixel){
            case 'k':return SDL_Color{7,10,12,255};
            case 'd':return SDL_Color{40,45,48,255};
            case 'g':return SDL_Color{78,84,87,255};
            case 'l':return SDL_Color{153,157,155,255};
            case 'w':return SDL_Color{236,232,217,255};
            case 's':return SDL_Color{224,171,116,255};
            case 'v':return SDL_Color{120,116,68,255};
            case 'r':return SDL_Color{211,46,48,255};
            case 'b':return SDL_Color{148,100,60,255};
            case 'n':return SDL_Color{53,65,81,255};
            case 'y':return SDL_Color{249,213,50,255};
            case 'h':return SDL_Color{37,29,25,255};
            case 'p':return SDL_Color{172,161,147,255};
            case 'c':return SDL_Color{50,48,47,255};
            case 't':return SDL_Color{83,77,72,255};
            case 'a':return SDL_Color{78,82,66,255};
            case 'f':return SDL_Color{132,143,128,255};
            case 'u':return SDL_Color{42,66,69,255};
            case 'e':return SDL_Color{255,186,54,255};
            default:return SDL_Color{242,138,32,255};
        }
    };
    const auto paint=[&](const auto& rows,int x,int y){
        for(std::size_t row=0;row<rows.size();++row)
            for(std::size_t col=0;col<std::min<std::size_t>(32,rows[row].size());++col){
                const char pixel=rows[row][col];
                if(pixel=='.')continue;
                const auto shade=color(pixel);
                const SDL_Rect dot{x+int(col),y+int(row),1,1};
                SDL_FillRect(atlas,&dot,SDL_MapRGBA(atlas->format,shade.r,shade.g,shade.b,255));
            }
    };
    for(int frame=0;frame<3;++frame)paint(supply_art::Soldier,frame*32,0);
    paint(supply_art::LegsA,0,24);paint(supply_art::LegsB,32,24);paint(supply_art::JumpLegs,64,24);
    paint(supply_art::Toaster,96,0);paint(supply_art::Cone,128,0);
    paint(supply_art::Pizza,160,0);paint(supply_art::Seagull,192,0);
    paint(supply_art::LowBatteryGamePad,224,0);paint(supply_art::MysteryTeddy,256,0);
    paint(supply_art::Juggernog,288,0);
    paint(supply_art::MobPlane,320,0);paint(supply_art::Brutus,352,0);
    paint(supply_art::DuckSoldier,384,0);paint(supply_art::SeagullFlap,416,0);
    paint(supply_art::BuriedWitch,448,0);paint(supply_art::Panzer,480,0);
    paint(supply_art::LisaHead,0,76);paint(supply_art::LisaDanceA,0,94);
    paint(supply_art::LisaHead,32,76);paint(supply_art::LisaDanceB,32,94);
    // Cache the score digits once: no per-frame font rasterization or uploads.
    for(int digit=0;digit<10;++digit){
        const char label[]{char('0'+digit),0};
        SDL_Surface* glyph=TTF_RenderUTF8_Blended(bold20_,label,White);
        if(glyph){
            SDL_Rect target{digit*18+(18-glyph->w)/2,42,glyph->w,glyph->h};
            SDL_BlitSurface(glyph,nullptr,atlas,&target);
            SDL_FreeSurface(glyph);
        }
    }
    waitingAtlas_=SDL_CreateTextureFromSurface(renderer_,atlas);
    SDL_FreeSurface(atlas);
    if(waitingAtlas_){
        SDL_SetTextureBlendMode(waitingAtlas_,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(waitingAtlas_,SDL_ScaleModeNearest);
    }
}
void Ui::waitingGameInput(std::uint32_t buttons,std::uint32_t held,TouchSample touch){
    if(operationScreenActive_&&(buttons&button::X)){
        supplyRunDisplay_.toggle(SDL_GetTicks());
        supplyRun_.setDucking(false);
        drawLoading(operationTitle_,operationDetail_,operationProgress_,operationCancellable_,
                    operationCancelLabel_.c_str(),true);
        return;
    }
    if(!downloadAnimationActive_)return;
    const bool touchDuck=touch.valid&&touch.touching&&waitingDuckTouch_.contains(touch.x,touch.y);
    supplyRun_.setDucking(((buttons|held)&button::Down)||touchDuck);
    if(buttons&(button::A|button::Up))supplyRun_.queueJump();
}
void Ui::drawWaitingGame(std::uint32_t now){
    if(!waitingAtlas_)return;
    supplyRun_.update(now);
    const SDL_Rect arena{64,336,1152,224};
    SDL_RenderSetClipRect(renderer_,&arena);
    const int ground=548;
    // A few rectangles provide parallax scenery without textures or file I/O.
    SDL_SetRenderDrawColor(renderer_,25,30,33,255);
    for(int i=0;i<10;++i){
        const int x=64+(i*157-int(supplyRun_.distance()/7)%157);
        const int height=28+(i%4)*17;
        const SDL_Rect building{x,ground-height,110,height};
        SDL_RenderFillRect(renderer_,&building);
    }
    SDL_SetRenderDrawColor(renderer_,123,128,127,255);
    SDL_RenderDrawLine(renderer_,64,ground,1216,ground);
    for(int i=0;i<25;++i){
        const int x=64+(i*49-int(supplyRun_.distance())%49);
        const SDL_Rect pebble{x,ground+5+(i%2)*3,4,3};
        SDL_RenderFillRect(renderer_,&pebble);
    }
    const auto sprite=[&](int index,int x,int bottom,int width,int height){
        const SDL_Rect source{index*32,0,32,36};
        const SDL_Rect target{x,bottom-height,width,height};
        SDL_RenderCopy(renderer_,waitingAtlas_,&source,&target);
    };
    const int frame=supplyRun_.height()>0?2:(now/120)%2;
    if(supplyRun_.invincible()){
        SDL_SetTextureColorMod(waitingAtlas_,255,224,100);
        SDL_SetRenderDrawColor(renderer_,249,213,50,190);
        const SDL_Rect aura{64+int(SupplyRun::PlayerX)-18,ground-int(supplyRun_.height())-76,72,80};
        SDL_RenderDrawRect(renderer_,&aura);
    }
    if(supplyRun_.ducking()){
        const SDL_Rect source{384,0,32,18};
        const SDL_Rect target{64+int(SupplyRun::PlayerX)-14,ground-36,64,36};
        SDL_RenderCopy(renderer_,waitingAtlas_,&source,&target);
    }else sprite(frame,64+int(SupplyRun::PlayerX)-14,ground-int(supplyRun_.height()),64,72);
    SDL_SetTextureColorMod(waitingAtlas_,255,255,255);
    for(const auto& obstacle:supplyRun_.obstacles()){
        const auto& definition=SupplyRun::Obstacles[obstacle.kind];
        const int bottom=ground-int(SupplyRun::obstacleLift(obstacle))+(obstacle.kind==3?int((now/160)%2)*3:0);
        const int spriteX=obstacle.kind>=9?448+int(obstacle.kind-9)*32:
                         obstacle.kind==3&&(now/160)%2?416:96+int(obstacle.kind)*32;
        const SDL_Rect source{spriteX,0,32,definition.spriteHeight};
        const int height=source.h*2;
        const SDL_Rect target{64+int(obstacle.x),bottom-height,64,height};
        SDL_RenderCopy(renderer_,waitingAtlas_,&source,&target);
    }
    if(supplyRun_.powerupVisible()||supplyRun_.invincible()){
        const SDL_Rect source{int((now/170)%2)*32,76,32,36};
        const SDL_Rect target=supplyRun_.invincible()?SDL_Rect{1064,400,64,72}:
            SDL_Rect{64+int(supplyRun_.powerupX())-10,ground-72,64,72};
        SDL_RenderCopy(renderer_,waitingAtlas_,&source,&target);
        if(supplyRun_.powerupVisible()&&!supplyRun_.invincible())
            text(regular18_,"POWER-UP",target.x-12,target.y-28,{249,213,50,255},110);
    }
    const SupplyRun::Obstacle* next=nullptr;
    for(const auto& obstacle:supplyRun_.obstacles()){
        if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
        if(!next||obstacle.x<next->x)next=&obstacle;
    }
    if(supplyRun_.invincible())text(bold20_,"LISA DANCE SHIELD  ·  "+std::to_string(int(std::ceil(supplyRun_.powerupSeconds())))+"s  ·  +100 POINTS",W/2,365,{249,213,50,255},1040,true);
    else if(next)text(regular18_,"INCOMING: "+std::string(SupplyRun::Obstacles[next->kind].joke)+(SupplyRun::overhead(next->kind)?"  [DUCK]":"  [JUMP]"),W/2,365,Muted,1040,true);
    SDL_RenderSetClipRect(renderer_,nullptr);
    const auto digits=[&](unsigned value,int right,SDL_Color color){
        SDL_SetTextureColorMod(waitingAtlas_,color.r,color.g,color.b);
        for(int place=0;place<6;++place){
            const SDL_Rect source{int(value%10)*18,42,18,28};
            const SDL_Rect target{right-18*(place+1),300,18,28};
            SDL_RenderCopy(renderer_,waitingAtlas_,&source,&target);
            value/=10;
        }
        SDL_SetTextureColorMod(waitingAtlas_,255,255,255);
    };
    digits(supplyRun_.score(),988,accentColor());
    digits(supplyRun_.best(),1216,LightText);
    if(supplyRun_.crashed()){
        roundRect({420,383,490,108},8,{16,20,23,240});
        text(bold28_,"Run over. One more?",665,394,White,460,true);
        text(regular18_,"Press A or tap to restart",665,446,LightText,460,true);
    }
}
void Ui::animateDownloadWaiting(){
    if(!operationScreenActive_)return;
    const auto now=SDL_GetTicks();
    if(now-downloadAnimationLastDraw_<33)return;
    if(operationLayoutTransition_){
        drawLoading(operationTitle_,operationDetail_,operationProgress_,operationCancellable_,
                    operationCancelLabel_.c_str(),true);
        return;
    }
    if(downloadAnimationActive_&&downloadScreenCache_)renderWaitingGameFrame(now);
}
void Ui::renderWaitingGameFrame(std::uint32_t now){
    if(!downloadAnimationActive_||!downloadScreenCache_)return;
    downloadAnimationLastDraw_=now;
    pump();
    pendingTouchIdentity_=touchIdentity_;pendingTouchTargets_=touchTargets_;
    SDL_RenderCopy(renderer_,downloadScreenCache_,nullptr,nullptr);
    drawWaitingGame(now);
    present();
}
void Ui::loading(const std::string& title,const std::string& detail,int progress,bool cancellable,const char* cancelLabel){
    drawLoading(title,detail,progress,cancellable,cancelLabel,false);
}
void Ui::downloadLoading(const std::string& title,const std::string& detail,int progress,bool cancellable,const char* cancelLabel){
    drawLoading(title,detail,progress,cancellable,cancelLabel,true);
}
void Ui::installLoading(const std::string& title,const std::string& detail,int progress){
    drawLoading(title,detail,progress,false,"",true);
}
void Ui::drawLoading(const std::string& rawTitle,const std::string& rawDetail,int progress,
                     bool cancellable,const char* cancelLabel,bool miniGame){
    const auto friendly=installProgressFeedback(rawTitle,rawDetail);
    operationScreenActive_=miniGame;
    if(miniGame){
        operationTitle_=rawTitle;
        operationDetail_=rawDetail;
        operationProgress_=progress;
        operationCancellable_=cancellable;
        operationCancelLabel_=cancelLabel;
    }
    const auto now=SDL_GetTicks();
    operationLayoutTransition_=miniGame&&supplyRunDisplay_.transitioning(now);
    const bool playable=miniGame&&waitingAtlas_&&downloadScreenCache_;
    const float gameAmount=playable?supplyRunDisplay_.amount(now):0.0f;
    const bool cached=playable&&gameAmount>=1.0f&&!operationLayoutTransition_&&
                      SDL_SetRenderTarget(renderer_,downloadScreenCache_)==0;
    if(gameAmount>0&&!downloadAnimationActive_)supplyRun_.resume(now);
    pump();
    beginTouch(miniGame?(cancellable?"loading-cancel":"loading-safe"):"loading-normal");
    backdrop();
    const SDL_Rect mark{26,4,94,94};
    SDL_RenderCopy(renderer_,logo_,nullptr,&mark);
    text(regular22_,gameName(config::activeGame),136,35,LightText,900);
    const std::string build=std::string(config::AppVersion)+" · r"+std::to_string(config::AppReleaseRevision);
    text(regular18_,build,W-32-measure(regular18_,build).first,38,Muted);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,32,100,1248,100);
    const int shown=std::clamp(progress,0,100);
    if(!miniGame){
        text(bold28_,friendly.title,W/2,273,White,1120,true);
        roundRect({256,354,768,12},5,raisedColor());
        if(progress>=0&&shown)roundRect({256,354,std::max(12,768*shown/100),12},5,accentColor());
        text(bold28_,progress<0?"Please wait...":std::to_string(shown)+"%",W/2,385,White,1008,true);
        wrapped(regular18_,friendly.message,{256,443,768,182},LightText);
        if(cancellable)footer({{"B",cancelLabel}});
        else footer({});
        downloadAnimationActive_=false;
        present();
        return;
    }

    std::string detailMessage,elapsed,waitNotice,line;
    std::istringstream detailLines(friendly.message);
    while(std::getline(detailLines,line)){
        if(line.rfind("Elapsed ",0)==0){elapsed=line;continue;}
        if(line.rfind("This step is taking longer",0)==0){waitNotice=line;continue;}
        if(!detailMessage.empty())detailMessage+="  |  ";
        detailMessage+=line;
    }

    // Only a visibility change redraws these full frames. Normal gameplay
    // keeps the existing cached background and bounded 30 Hz sprite updates.
    if(gameAmount>0){
        SDL_SetRenderDrawColor(renderer_,43,49,53,255);
        SDL_RenderDrawLine(renderer_,64,287,1216,287);
        text(bold20_,"SUPPLY RUN",64,300,accentColor(),400);
        text(bold20_,"SCORE",794,300,Muted,84);
        text(bold20_,"BEST",1020,300,Muted,84);
        text(regular22_,"Jump: A / Up or tap. Duck: hold Down or the Duck button.",W/2,581,White,1152,true);
        text(regular18_,(elapsed.empty()?std::string{}:elapsed+"  |  ")+"Setup continues while you play.",
             W/2,618,Muted,1152,true);
        if(!cached)drawWaitingGame(now);
        if(gameAmount<1.0f){
            const auto shade=darkColor();
            SDL_SetRenderDrawColor(renderer_,shade.r,shade.g,shade.b,
                                   static_cast<Uint8>(std::lround(255*(1.0f-gameAmount))));
            const SDL_Rect fade{32,287,1216,371};
            SDL_RenderFillRect(renderer_,&fade);
        }
    }

    const float hidden=1.0f-gameAmount;
    const auto move=[hidden](int game,int centered){
        return int(std::lround(game+(centered-game)*hidden));
    };
    const int titleWidth=std::min(measure(bold28_,friendly.title).first,1120);
    text(bold28_,friendly.title,move(64,(W-titleWidth)/2),move(122,273),White,1120);
    const SDL_Rect bar{move(64,256),move(185,354),move(1058,768),12};
    roundRect(bar,5,raisedColor());
    if(progress>=0&&shown)roundRect({bar.x,bar.y,std::max(12,bar.w*shown/100),bar.h},5,accentColor());
    const std::string percent=progress<0?(hidden>.5f?"Please wait...":"—"):std::to_string(shown)+"%";
    const int percentWidth=measure(bold28_,percent).first;
    text(bold28_,percent,move(1140,(W-percentWidth)/2),move(168,385),White,move(88,1008));
    const SDL_Rect detailBox{move(64,256),move(211,443),move(1152,768),move(48,182)};
    wrapped(regular18_,hidden>.5f?friendly.message:detailMessage,detailBox,LightText);
    if(!waitNotice.empty()&&gameAmount>.5f)text(regular18_,waitNotice,64,260,Muted,1152);

    if(supplyRunDisplay_.enabled()&&playable){
        if(gameAmount>.95f)touchTarget({64,336,1152,280},TouchActionKind::Button,int(button::A));
        if(cancellable)footer({{"A/UP","Jump"},{"DOWN","Hold to duck"},{"X/1","Hide game"},{"B",cancelLabel}});
        else footer({{"A/UP","Jump"},{"DOWN","Hold to duck"},{"X/1","Hide game"}});
        const int columnWidth=W/(cancellable?4:3);
        waitingDuckTouch_={columnWidth+8,663,columnWidth-16,52};
    }else{
        if(cancellable)footer({{"X/1","Show Supply Run"},{"B",cancelLabel}});
        else footer({{"X/1","Show Supply Run"}});
    }
    if(cached){
        SDL_SetRenderTarget(renderer_,nullptr);
        SDL_RenderCopy(renderer_,downloadScreenCache_,nullptr,nullptr);
    }
    downloadAnimationActive_=cached;
    downloadAnimationLastDraw_=now;
    if(cached)drawWaitingGame(now);
    present();
}
int Ui::modal(const std::string& title,const std::string& body,bool isError,
              const std::string& actionLabel,int scrollOffset,bool confirm){
    pump();beginTouch("modal|"+title+"|"+body+"|"+actionLabel+(confirm?"|confirm":""));
    backdrop();header(isError?"Attention required":"Unbound HQ");
    quietPanel({148,156,984,454});
    roundRect({148,174,3,418},1,isError?Red:accentColor());
    const SDL_Rect art{180,172,50,50};
    SDL_RenderCopy(renderer_,squareLogo_,nullptr,&art);
    text(bold28_,title,246,180,isError?Red:White,850);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,182,233,1098,233);
    const SDL_Rect bodyBox{182,257,890,316};
    const int maximum=wrappedScrollable(regular18_,body,bodyBox,LightText,scrollOffset);
    const int offset=std::clamp(scrollOffset,0,maximum);
    scrollBar(bodyBox,offset,maximum);
    std::vector<std::pair<std::string,std::string>> actions{{"A",actionLabel}};
    if(maximum>0)actions.emplace_back("UP/DN","Scroll");
    if(confirm)actions.emplace_back("B","Cancel");
    footer(actions);
    present();return maximum;
}
void Ui::actionCard(SDL_Rect r,const std::string& eyebrow,const std::string& title,const std::string& detail,bool selected,SDL_Color accent,bool enabled){quietPanel(r,selected);text(regular18_,eyebrow,r.x+20,r.y+18,selected&&enabled?accentColor():Muted,r.w-40);text(bold20_,title,r.x+20,r.y+55,enabled?White:Muted,r.w-50);wrapped(regular18_,detail,{r.x+20,r.y+94,r.w-40,r.h-112},enabled?LightText:Muted);if(selected)directionIcon({r.x+r.w-34,r.y+58,20,20},button::Right,enabled?accentColor():Muted);(void)accent;}
void Ui::home(const GameInstall& install,const Release* release,const std::string& changes,int selected,const std::string& updateStatus,bool repairNeeded,bool restartRequired,int gameChoice,unsigned mapUpdates){
    pump();beginTouch(config::ghostsMode()?"home-ghosts":"home");backdrop();header("",true,gameChoice);
    const bool contentFocused=!navigationFocused_;
    text(bold38_,config::ghostsMode()?"CALL OF DUTY: GHOSTS":"BLACK OPS II",244,116,White,1008);
    const std::string version=install.modVersion.empty()?"Not installed":install.modVersion;
    const std::string region=install.sku?regionName(install.sku->region):"Region unavailable";
    text(regular18_,"Unbound "+version+" · "+region+" · "+displayLanguage(install.languageFolder),244,174,Muted,1008);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,244,207,1252,207);
    SDL_RenderDrawLine(renderer_,872,227,872,540);

    const SDL_Rect article{244,220,610,320};
    if(contentFocused&&selected==0){
        roundRect({article.x-4,article.y-4,article.w+8,article.h+8},8,accentColor());
        roundRect(article,6,darkColor());
    }
    touchTarget(article,TouchActionKind::HomeAction,0);
    artworkCover(newsHero_?newsHero_:boot_,{248,224,602,150});
    text(bold20_,"WHAT'S NEW",248,385,accentColor(),602);
    const std::string preview=preparedNewsSource_==changes?preparedNewsPreview_:plainNewsPreview(changes);
    std::istringstream lines(preview);
    std::string headline,description,line;
    while(std::getline(lines,line)){
        line=trim(line);
        if(line.empty())continue;
        if(headline.empty())headline=line;
        else if(description.empty())description=line;
    }
    if(headline.empty())headline=config::ghostsMode()?"Ghosts UNC":"Unbound";
    if(description.empty())description=updateStatus.empty()?"Read the latest release notes.":updateStatus;
    text(bold28_,headline,248,416,White,602);
    wrapped(regular18_,description,{248,460,602,48},LightText);
    text(regular18_,"Read release notes  >",248,511,accentColor(),602);

    text(bold20_,"Your installation",896,225,White,346);
    const bool canUpdate=install.update.present&&install.destinationReady;
    const bool coreUpdate=release&&canUpdate&&(version=="Not installed"||compareVersions(version,release->version)<0);
    const bool updateAvailable=coreUpdate||mapUpdates>0;
    const bool current=release&&canUpdate&&!updateAvailable&&version!="Not installed";
    const std::string state=repairNeeded?"Repair required":restartRequired?"Restart required":
        updateAvailable?(coreUpdate?"Core update available":"Map pack updates available"):current?"Up to date":canUpdate?"Release check unavailable":"Select an update location";
    if(current&&!repairNeeded&&!restartRequired)checkmark({896,263,17,17},Green);
    text(regular18_,state,current&&!repairNeeded&&!restartRequired?923:896,258,
         repairNeeded?Red:current?Green:Muted,346);
    struct StatusRow {std::string label,value;SDL_Color color;};
    const std::array<StatusRow,5> rows{{
        {"Core",version,LightText},
        {"Official update",officialUpdateSummary(install),officialUpdateStatus(install)==OfficialUpdateStatus::Current?Green:accentColor()},
        {"Game",install.base.present?deviceName(install.base.device):"Not found",install.base.present?LightText:Muted},
        {"Storage",install.update.present?deviceName(install.update.device):"Not found",canUpdate?LightText:Muted},
        {"Language",displayLanguage(install.languageFolder),LightText}
    }};
    for(std::size_t i=0;i<rows.size();++i){
        const int y=294+int(i)*29;
        text(regular18_,rows[i].label,896,y,Muted,134);
        text(regular18_,rows[i].value,1044,y,rows[i].color,198);
        SDL_SetRenderDrawColor(renderer_,43,49,53,255);
        SDL_RenderDrawLine(renderer_,896,y+29,1242,y+29);
    }
    const SDL_Rect launch{896,450,346,52};
    primaryAction(launch,repairNeeded?"Repair before playing":restartRequired?"Restart before playing":"Launch Game",
                  contentFocused&&selected==4);
    touchTarget(launch,TouchActionKind::HomeAction,4);
    const SDL_Rect manage{896,513,346,31};
    if(contentFocused&&selected==1)quietPanel(manage,true);
    const std::string installNote=repairNeeded?"Resume installation":updateAvailable?"Update installed content  >":"Manage installation  >";
    text(regular18_,installNote,906,517,contentFocused&&selected==1?accentColor():LightText,326);
    touchTarget(manage,TouchActionKind::HomeAction,1);
    networkSummary(contentFocused&&selected==5);
    navigationRail(0);
    if(gameChoice>=0)footer({{"A","Select game"},{"DPAD-LR","Choose game"},{"B","Back"},{"+","Exit"}});
    else if(navigationFocused_)footer({{"A","Open section"},{"UP/DN","Choose section"},{"L","Switch game"},{"+","Exit"}});
    else footer({{"A",selected==4?"Launch game":selected==1?"Installation":selected==5?"Network":"Release notes"},
                 {"D-PAD","Navigate"},{"L","Switch game"},{"B","Sidebar"},{"+","Exit"}});
    present();
}
void Ui::installSteps(int current){
    navigationRail(1,false);
    const std::array<std::string,3> steps{{"Choose content","Review","Install"}};
    for(int i=0;i<3;++i){
        const int x=244+i*337;
        circle(x+12,230,12,i<=current?accentColor():raisedColor());
        if(i<current)checkmark({x+4,222,16,16},config::ghostsMode()?Ink:White);
        else text(regular18_,std::to_string(i+1),x+12,217,i==current?(config::ghostsMode()?Ink:White):Muted,0,true);
        text(regular18_,steps[i],x+37,217,i==current?White:Muted,205);
        if(i<2){
            SDL_SetRenderDrawColor(renderer_,43,49,53,255);
            SDL_RenderDrawLine(renderer_,x+220,230,x+312,230);
        }
    }
}

void Ui::contentQueue(const std::vector<std::pair<std::string,std::string>>& rows,
                      const GameInstall& install,const std::string& total,int scroll){
    pump();beginTouch("content-queue");backdrop();header("");installSteps(1);
    text(bold38_,"Ready to install",244,116,White,1008);
    text(regular18_,"Core first, followed by your selected map packs.",244,173,Muted,1008);
    const SDL_Rect clip=ContentQueueViewport;
    SDL_RenderSetClipRect(renderer_,&clip);
    for(std::size_t i=0;i<rows.size();++i){
        const int y=clip.y+8+int(i)*80-scroll;
        if(y+70<clip.y||y>clip.y+clip.h)continue;
        text(bold20_,rows[i].first,260,y,White,574);
        text(regular18_,rows[i].second,260,y+33,Muted,574);
        SDL_SetRenderDrawColor(renderer_,43,49,53,255);
        SDL_RenderDrawLine(renderer_,260,y+70,834,y+70);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,872,266,872,645);
    text(bold20_,"Your installation",896,270,White,346);
    text(regular18_,"Destination",896,320,Muted,346);
    text(bold20_,deviceName(install.update.device),896,347,White,346);
    text(regular18_,"Language / region",896,392,Muted,346);
    text(regular18_,displayLanguage(install.languageFolder)+" / "+(install.sku?regionName(install.sku->region):"Unknown"),896,420,LightText,346);
    text(regular18_,"Package total",896,471,Muted,346);
    text(bold28_,total,896,498,accentColor(),346);
    wrapped(regular18_,"Local ZIPs and saved downloads are verified before use.",{244,583,606,60},Muted);
    const SDL_Rect start{896,590,346,54};
    primaryAction(start,"Start installation",true);
    touchTarget(start,TouchActionKind::Button,int(button::A));
    footer({{"A","Start installation"},{"UP/DN","Scroll queue"},{"B","Change selection"}});present();
}

void Ui::mapPacks(const std::vector<MapPack>& packs,const std::vector<bool>& selected,
                  const std::vector<std::string>& versions,int focused,
                  const GameInstall& install,const std::string& notice,bool reinstall){
    pump();beginTouch("map-packs|"+install.languageFolder);backdrop();header("");installSteps(0);
    text(bold38_,"Choose your content",244,116,White,1008);
    text(regular18_,"Keep Core installed. Add the map packs you want to play.",244,173,Muted,1008);
    quietPanel({244,263,1008,52});
    checkmark({264,280,20,20},accentColor());
    text(bold20_,"Unbound Core",300,275,White,310);
    text(regular18_,"Required",610,277,Muted,160);
    text(regular18_,deviceName(install.update.device),965,277,LightText,267);

    const int count=int(packs.size());
    const int selectedCount=int(std::count(selected.begin(),selected.end(),true));
    const int start=std::clamp(focused-2,0,std::max(0,count-3));
    text(bold20_,"Optional map packs",244,336,White,350);
    text(regular18_,std::to_string(selectedCount)+" selected",638,338,Muted,134);
    for(int i=start;i<count&&i<start+3;++i){
        const int y=375+(i-start)*64;
        const SDL_Rect row{244,y,530,58};
        if(i==focused)quietPanel(row,true);
        const bool checked=i<int(selected.size())&&selected[i];
        roundRect({260,y+17,24,24},4,checked?accentColor():PanelBorder);
        if(checked)checkmark({263,y+20,18,18},config::ghostsMode()?Ink:White);
        else roundRect({262,y+19,20,20},3,darkColor());
        text(bold20_,packs[i].name,300,y+2,White,454);
        const std::string version=i<int(versions.size())?versions[i]:"";
        text(regular18_,version.empty()?"Available to install":reinstall&&checked?"Reinstall queued / v"+version:"Installed v"+version,300,y+30,version.empty()?Muted:Green,454);
        touchTarget(row,TouchActionKind::MapPackRow,i);
    }
    if(count>3){

        directionIcon({687,574,18,18},button::Up,Muted);
        directionIcon({733,574,18,18},button::Down,Muted);
        touchTarget({678,561,36,38},TouchActionKind::Button,int(button::Up));
        touchTarget({724,561,36,38},TouchActionKind::Button,int(button::Down));
    }
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,794,338,794,646);
    if(count){
        const int i=std::clamp(focused,0,count-1);
        const auto& pack=packs[i];
        const auto version=i<int(versions.size())?versions[i]:std::string{};
        const auto* release=mapPackRelease(pack,version,reinstall);
        text(bold28_,pack.name,818,335,White,424);
        wrapped(regular18_,pack.description,{818,382,424,108},LightText);
        text(regular18_,release?displayPackageSize(release->compressedBytes)+" package":"Installed version is current",818,503,Muted,424);
        const SDL_Rect details{818,539,424,38};
        text(regular18_,"Full description  >",818,545,accentColor(),424);
        touchTarget(details,TouchActionKind::MapPackDetails,i);
    }else{
        text(regular18_,"No optional packs available",244,390,Muted,530);
        wrapped(regular18_,"Continue to review your Core installation.",{818,382,424,108},LightText);
    }
    if(!notice.empty())text(regular18_,notice,244,561,Muted,count>3?400:530);
    const SDL_Rect review{896,594,346,54};
    primaryAction(review,reinstall?"Review reinstallation":"Review installation",focused==count);
    touchTarget(review,TouchActionKind::MapPackRow,count);
    if(count){
        const SDL_Rect repair{244,598,530,50};
        quietPanel(repair,focused==count+1);
        roundRect({260,612,23,23},4,reinstall?accentColor():PanelBorder);
        if(reinstall)checkmark({263,615,17,17},config::ghostsMode()?Ink:White);
        else roundRect({262,614,19,19},3,darkColor());
        text(bold20_,"Reinstall selected packs",300,610,reinstall?accentColor():LightText,450);
        touchTarget(repair,TouchActionKind::MapPackRow,count+1);
    }
    footer({{"A","Select"},{"D-PAD","Navigate"},{"RIGHT","Details"},{"X","Reinstall"},{"B","Back"}});present();
}

void Ui::installReview(const GameInstall& active,const std::string& latest,
                       const std::string& notice,int selected,bool enabled,
                       bool sdAvailable,bool repair,bool update,bool mapsAvailable){
    pump();beginTouch("install-review|"+active.update.root+"|"+active.languageFolder);
    backdrop();header("");navigationRail(1);
    text(bold38_,repair?"Resume installation":"Install / Update",244,116,White,1008);
    wrapped(regular18_,notice.empty()?"Your game, your content. Review the destination before continuing.":notice,{244,173,1008,65},Muted);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,872,256,872,645);
    text(bold20_,"Your destination",244,256,White,610);
    const auto row=[&](const std::string& label,const std::string& value,int y,SDL_Color shade){
        text(regular18_,label,244,y,Muted,180);
        text(regular22_,value,440,y,shade,414);
        SDL_SetRenderDrawColor(renderer_,43,49,53,255);
        SDL_RenderDrawLine(renderer_,244,y+38,854,y+38);
    };
    row("Update storage",active.update.present?deviceName(active.update.device):"Not found",306,White);
    row("Language",displayLanguage(active.languageFolder),359,White);
    row("Region",active.sku?active.sku->label:"Not identified",412,LightText);
    row("Official update",officialUpdateSummary(active),465,
        officialUpdateStatus(active)==OfficialUpdateStatus::Current?Green:accentColor());
    const SDL_Rect settings{244,529,610,52};
    quietPanel(settings,selected==0);
    text(regular22_,"Change storage or language",264,540,selected==0?accentColor():LightText,550);
    directionIcon({814,546,20,20},button::Right,Muted);
    touchTarget(settings,TouchActionKind::InstallRow,0);
    const bool mapsEnabled=mapsAvailable&&!repair&&!active.modVersion.empty();
    const SDL_Rect maps{244,598,610,50};
    quietPanel(maps,selected==5);
    text(bold20_,"Reinstall map packs",264,609,mapsEnabled?(selected==5?accentColor():LightText):Muted,550);
    directionIcon({814,613,20,20},button::Right,mapsEnabled?accentColor():Muted);
    if(mapsEnabled)touchTarget(maps,TouchActionKind::InstallRow,5);
    text(bold20_,update?"Core update available":"Unbound Core",896,256,White,346);
    text(regular18_,"Installed",896,306,Muted,150);
    text(regular18_,active.modVersion.empty()?"Not installed":active.modVersion,1060,306,LightText,182);
    text(regular18_,"Latest release",896,344,Muted,150);
    text(bold28_,latest.empty()?"Unavailable":latest,896,376,accentColor(),346);
    wrapped(regular18_,sdAvailable?"Update ZIP found on your SD card. HQ will verify and install its files.":
        "Choose Core and optional map packs. Every package is verified before installation.",{896,435,346,100},LightText);
    const SDL_Rect install{896,548,346,54};
    primaryAction(install,repair?"Resume installation":sdAvailable?"Install from SD":"Choose content",selected==3,enabled);
    if(enabled)touchTarget(install,TouchActionKind::InstallRow,3);
    if(sdAvailable&&enabled){
        const SDL_Rect internet{896,615,346,34};
        if(selected==4)quietPanel(internet,true);
        text(regular18_,"Use internet instead",906,620,selected==4?accentColor():Muted,326);
        touchTarget(internet,TouchActionKind::InstallRow,4);
    }
    if((!enabled&&(selected==3||selected==4))||(selected==5&&!mapsEnabled))suppressedTouchButtons_|=button::A;
    footer({{"A","Select"},{"D-PAD","Navigate"},{"Y/2","Map packs"},{"X/1","Settings"},{"B","Back"}});present();
}
void Ui::installMenu(const GameInstall& active,const std::vector<InstallOption>& options,
                     const InstallPreferences& draft,int selectedRow,
                     const std::string& latest,const std::string& notice,
                     bool contentAllowed,bool hasUpdatePlan,bool reinstallAvailable,bool inheritedRepairNeeded){
    pump();beginTouch("install|"+active.update.root+"|"+draft.languageFolder);
    backdrop();header("");navigationRail(1);
    text(bold38_,"Install settings",244,116,White,1008);
    const bool manualTarget=draft.updateDevice!=Device::Missing||draft.updateTitleId!=0;
    const bool manualLanguage=!draft.languageFolder.empty();
    const bool ready=active.sku&&active.update.present&&active.destinationReady;
    const bool installEnabled=ready&&contentAllowed&&(hasUpdatePlan||reinstallAvailable||inheritedRepairNeeded);
    const std::string message=!ready&&!active.destinationWarning.empty()?active.destinationWarning:
        (!notice.empty()?notice:(!contentAllowed?"Restart HQ and complete its app update before changing game content.":
         (draft.languageInstallPending?"Language change pending. Install the full release and current patches for this folder.":
          "Choose storage and language. Save your selection before installing.")));
    wrapped(regular18_,message,{244,173,1008,65},!ready?Red:Muted);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);
    SDL_RenderDrawLine(renderer_,872,256,872,645);
    std::size_t languageCount=0;
    bool manualTargetAvailable=false;
    for(const auto& option:options){
        if(option.install.sku&&option.install.sku->updateTitleId==draft.updateTitleId&&
           option.install.update.device==draft.updateDevice)manualTargetAvailable=true;
        if(option.install.sku&&active.sku&&option.install.sku->updateTitleId==active.sku->updateTitleId&&
           option.install.update.device==active.update.device)languageCount=option.languages.size();
    }
    const auto editableRow=[&](int index,int y,const std::string& title,const std::string& value,
                                const std::string& detail,bool enabled){
        const SDL_Rect row{244,y,610,91};
        const bool focused=selectedRow==index&&!navigationFocused_;
        quietPanel(row,focused);
        text(regular18_,title,264,y+6,Muted,470);
        text(bold20_,value,264,y+32,enabled?White:Muted,454);
        text(regular18_,detail,264,y+62,Muted,570);
        directionIcon({750,y+34,22,22},button::Left,focused&&enabled?accentColor():Muted);
        directionIcon({806,y+34,22,22},button::Right,focused&&enabled?accentColor():Muted);
        if(enabled){
            touchTarget({244,y,490,91},TouchActionKind::InstallRow,index);
            touchTarget({734,y,54,91},TouchActionKind::InstallChoice,index*2);
            touchTarget({790,y,64,91},TouchActionKind::InstallChoice,index*2+1);
        }
    };
    const GameSku* savedSku=manualTarget?findSkuByTitle(draft.updateTitleId):active.sku;
    std::string target=manualTarget?deviceName(draft.updateDevice):active.update.present?deviceName(active.update.device):"No update found";
    if(!manualTarget)target="Auto: "+target;
    editableRow(0,256,"Install destination",target,
        (savedSku?std::string(savedSku->label)+" / ":std::string{})+
        (manualTarget&&!manualTargetAvailable?"Unavailable":std::to_string(options.size())+" available locations"),!options.empty());
    editableRow(1,358,"Content language",manualLanguage?displayLanguage(draft.languageFolder):"Auto: "+displayLanguage(active.languageFolder),
        std::to_string(languageCount)+" available language folders",languageCount>0);
    text(regular18_,"Region",264,465,Muted,180);
    text(regular18_,active.sku?active.sku->label:"Not identified",460,465,LightText,374);
    const auto secondary=[&](SDL_Rect rect,const std::string& label,int index,bool enabled){
        quietPanel(rect,selectedRow==index&&!navigationFocused_);
        text(regular18_,label,rect.x+20,rect.y+13,enabled?(selectedRow==index?accentColor():LightText):Muted,rect.w-40);
        if(enabled)touchTarget(rect,TouchActionKind::InstallRow,index);
    };
    secondary({244,511,294,52},"Save selection",2,ready&&contentAllowed);
    const SDL_Rect rescan{556,511,298,52};
    quietPanel(rescan);
    text(regular18_,"Rescan game storage",576,526,LightText,258);
    touchTarget(rescan,TouchActionKind::Button,int(button::X));
    secondary({244,578,610,48},"Reset storage and language to Auto",4,true);
    text(bold20_,hasUpdatePlan?"Core update available":"Unbound Core",896,256,White,346);
    text(regular18_,"Installed",896,307,Muted,150);
    text(regular18_,active.modVersion.empty()?"Not installed":active.modVersion,1060,307,LightText,182);
    text(regular18_,"Latest release",896,343,Muted,346);
    text(bold28_,latest.empty()?"Unavailable":latest,896,374,accentColor(),346);
    text(regular18_,"Official update",896,430,Muted,346);
    text(regular18_,officialUpdateSummary(active),896,460,
        officialUpdateStatus(active)==OfficialUpdateStatus::Current?Green:accentColor(),346);
    text(regular18_,"Game: "+std::string(active.base.present?deviceName(active.base.device):"Not found"),896,502,Muted,346);
    const SDL_Rect install{896,548,346,54};
    primaryAction(install,inheritedRepairNeeded?"Restore game files":hasUpdatePlan?"Choose content":"Reinstall Core",
                  selectedRow==3&&!navigationFocused_,installEnabled);
    if(installEnabled)touchTarget(install,TouchActionKind::InstallRow,3);
    const std::string detail=inheritedRepairNeeded?"Uses your installed game files":draft.languageInstallPending?"Full release + current patches":
        !ready?"Select an available destination":!contentAllowed?"Complete the HQ update first":"USB or internal storage only";
    text(regular18_,detail,896,620,Muted,346);
    const std::array<bool,5> enabledRows{{!options.empty(),languageCount>0,ready&&contentAllowed,installEnabled,true}};
    if(selectedRow>=0&&selectedRow<5&&!enabledRows[std::size_t(selectedRow)])suppressedTouchButtons_|=button::A;
    footer({{"A",selectedRow<2?"Change":selectedRow==2?"Save":selectedRow==3?"Install":"Reset Auto"},
            {"D-PAD","Navigate / adjust"},{"X/1","Rescan"},{"B","Back"}},true);present();
}
void Ui::news(const std::string& changes,int scrollOffset){
    if(newsBlocks_.empty())prepareNews(changes);
    pump();beginTouch("news");backdrop();SDL_Rect clip{244,102,1008,556};SDL_RenderSetClipRect(renderer_,&clip);
    const int top=112-scrollOffset,bottom=newsPageBottom_-scrollOffset;
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_Rect sheet{244,std::max(102,top),1008,std::max(0,std::min(658,bottom)-std::max(102,top))};if(sheet.h>0)SDL_RenderFillRect(renderer_,&sheet);
    SDL_SetRenderDrawColor(renderer_,panelColor().r,panelColor().g,panelColor().b,panelColor().a);SDL_Rect inner{245,std::max(102,top+1),1006,std::max(0,std::min(658,bottom-1)-std::max(102,top+1))};if(inner.h>0)SDL_RenderFillRect(renderer_,&inner);
    const int documentTop=scrollOffset+NewsClipTop-4096;
    auto block=std::lower_bound(newsBlocks_.begin(),newsBlocks_.end(),documentTop,[](const NewsBlock& item,int y){return item.rect.y<y;});
    for(;block!=newsBlocks_.end();++block){if(block->rect.y-scrollOffset>=NewsClipBottom)break;SDL_Rect target=block->rect;target.y-=scrollOffset;if(!newsVisible(target.y,target.h))continue;if(block->kind==0&&block->texture)SDL_RenderCopy(renderer_,block->texture,nullptr,&target);else if(block->kind==1){SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,target.x,target.y,target.x+target.w,target.y);}else if(block->kind==2)roundRect(target,8,raisedColor());else if(block->kind==3)roundRect(target,6,raisedColor());}
    SDL_RenderSetClipRect(renderer_,nullptr);scrollBar({244,118,996,524},scrollOffset,newsMaximumScroll_);header("What's new",true);navigationRail(0);footer({{"UP/DN","Scroll / swipe page"},{"B","Back"}});present();
}

void Ui::prepareSupport(const std::string& report){
    for(auto& block:supportBlocks_)if(block.text.texture)SDL_DestroyTexture(block.text.texture);
    supportBlocks_.clear();preparedSupportSource_=report;
    constexpr std::size_t ChunkBytes=1536;
    constexpr unsigned ChunkLines=32;
    constexpr int Width=960;
    int cursor=0;std::string chunk;unsigned chunkLines=0;
    const auto flush=[&](){
        if(chunk.empty())return;
        SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(regular18_,chunk.c_str(),LightText,Width);
        if(surface){
            SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer_,surface);
            if(texture){
                SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
                supportBlocks_.push_back({{texture,surface->w,surface->h},cursor});
                cursor+=surface->h;
            }
            SDL_FreeSurface(surface);
        }
        chunk.clear();chunkLines=0;
    };
    const auto appendLine=[&](const std::string& line){
        if(chunk.size()+line.size()+1>ChunkBytes||chunkLines>=ChunkLines)flush();
        chunk+=line;chunk.push_back('\n');++chunkLines;
    };
    std::istringstream lines(report);std::string line;
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()){appendLine({});continue;}
        // Wrap even an unbroken URL/hash/path, which SDL_ttf's word wrapping
        // alone can leave wider than the viewport. This work occurs only when
        // the report changes, never while scrolling or downloading.
        std::size_t start=0;
        while(start<line.size()){
            std::vector<std::size_t> boundaries;
            const std::size_t limit=std::min(line.size(),start+ChunkBytes-1);
            for(std::size_t end=start+1;end<=limit;++end)
                if(end==line.size()||(static_cast<unsigned char>(line[end])&0xc0U)!=0x80U)
                    boundaries.push_back(end);
            if(boundaries.empty())boundaries.push_back(limit);
            std::size_t lower=0,upper=boundaries.size();
            while(lower<upper){
                const std::size_t middle=lower+(upper-lower)/2;
                int width=0,height=0;
                const std::string prefix=line.substr(start,boundaries[middle]-start);
                TTF_SizeUTF8(regular18_,prefix.c_str(),&width,&height);
                if(width<=Width)lower=middle+1;else upper=middle;
            }
            std::size_t end=boundaries[lower?lower-1:0];
            if(end<line.size()){
                const std::size_t space=line.find_last_of(" \t",end-1);
                if(space!=std::string::npos&&space>start)end=space;
            }
            appendLine(line.substr(start,end-start));start=end;
            while(start<line.size()&&(line[start]==' '||line[start]=='\t'))++start;
        }
    }
    flush();supportMaximumScroll_=std::max(0,cursor-430);
}

int Ui::support(const std::string& report,int scrollOffset,const std::string& status){
    if(preparedSupportSource_!=report||(supportBlocks_.empty()&&!report.empty()))prepareSupport(report);
    pump();beginTouch("support");backdrop();header("Support & diagnostics",true);
    quietPanel({244,121,1008,524});
    text(bold28_,"System information",268,136,White,940);
    const std::string message=status.empty()?"A exports HQ reports + Wii U crash logs to ZIP. Y / 2 refreshes.":status;
    text(regular18_,message,268,176,status.empty()?LightText:accentColor(),960);
    const SDL_Rect clip{268,207,960,430};SDL_RenderSetClipRect(renderer_,&clip);
    const int offset=std::clamp(scrollOffset,0,supportMaximumScroll_);
    const auto first=std::lower_bound(supportBlocks_.begin(),supportBlocks_.end(),offset,
        [](const SupportBlock& block,int top){return block.y+block.text.height<=top;});
    for(auto block=first;block!=supportBlocks_.end();++block){
        SDL_Rect target{clip.x,clip.y+block->y-offset,block->text.width,block->text.height};
        if(target.y>=clip.y+clip.h)break;
        SDL_RenderCopy(renderer_,block->text.texture,nullptr,&target);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);
    scrollBar(clip,offset,supportMaximumScroll_);
    navigationRail(4);
    footer({{"A","Export ZIP"},{"UP/DN","Scroll"},{"Y/2","Refresh"},{"B","Back"}});
    present();return supportMaximumScroll_;
}

void Ui::localModManager(const std::vector<LocalModInventoryItem>& mods,
                         std::size_t selected,int scrollOffset,
                         const std::string& notice,bool nativeSd,
                         Device consoleDevice,int detailsScroll){
    std::string touchIdentity="local-mods";
    for(const auto& mod:mods)touchIdentity+='|'+mod.folder;
    pump();beginTouch(touchIdentity);backdrop();header("Local Mod Manager",true);

    const auto duplicateCount=static_cast<unsigned>(std::count_if(
        mods.begin(),mods.end(),[](const LocalModInventoryItem& item){
            return item.hasSd&&item.hasConsole;
        }));
    const std::string consoleStorage=consoleDevice==Device::MLC?"INTERNAL":
        (consoleDevice==Device::USB?"USB":"UNKNOWN");
    const auto compactPill=[this](const std::string& label,int right,int y,
                                  SDL_Color fill){
        const auto measured=measure(regular18_,label);
        const int width=measured.first+20;
        SDL_Rect badge{right-width,y,width,28};
        roundRect(badge,5,panelColor());
        text(regular18_,label,badge.x+10,badge.y+1,fill);
        return badge.x-7;
    };

    text(bold28_,"Your mods",244,121,White,290);
    text(regular18_,"Up / Down selects a mod. Left / Right scrolls its details.",244,165,Muted,660);
    if(duplicateCount>0){
        const std::string summary=std::to_string(duplicateCount)+
            (duplicateCount==1?" shared folder needs attention":" shared folders need attention");
        text(regular18_,summary,454,131,accentColor(),455);
    }
    quietPanel({244,205,374,425});
    SDL_Rect clip{256,218,350,399};SDL_RenderSetClipRect(renderer_,&clip);
    if(mods.empty()){
        wrapped(regular22_,
            "No manifested mods were found on the SD card or in the installed game update.",
            {268,250,326,180},LightText);
    }
    for(std::size_t i=0;i<mods.size();++i){
        const auto& item=mods[i];
        const auto& display=item.hasSd&&(nativeSd||!item.hasConsole)
            ?item.sd:item.console;
        SDL_Rect row{258,225+int(i)*82-scrollOffset,346,70};
        if(row.y+row.h<218||row.y>617)continue;
        const bool rowFocused=i==selected;
        const bool duplicate=item.hasSd&&item.hasConsole;
        quietPanel(row,rowFocused);
        touchTarget(row,TouchActionKind::LocalModRow,static_cast<int>(i),&clip);
        int badgeRight=row.x+row.w-10;
        if(item.hasConsole)badgeRight=compactPill("CONSOLE",badgeRight,row.y+8,LightText);
        if(item.hasSd)badgeRight=compactPill("SD",badgeRight,row.y+8,accentColor());
        const int textWidth=std::max(120,badgeRight-(row.x+18));
        text(bold20_,display.name,row.x+18,row.y+8,White,textWidth);
        text(regular18_,"by "+display.author,row.x+18,row.y+38,
             Muted,textWidth);
        if(duplicate)text(regular18_,"!",row.x+row.w-20,row.y+39,accentColor());
    }
    SDL_RenderSetClipRect(renderer_,nullptr);

    quietPanel({636,205,616,425});
    localModDetailsMaximumScroll_=0;
    const SDL_Rect detailsClip{660,231,504,375};
    SDL_RenderSetClipRect(renderer_,&detailsClip);
    if(!mods.empty()){
        const auto& item=mods[std::min(selected,mods.size()-1)];
        const auto& display=item.hasSd&&(nativeSd||!item.hasConsole)
            ?item.sd:item.console;
        int badgeX=665;
        const int offset=std::max(0,detailsScroll);
        if(item.hasSd){
            pill("SD MOD",badgeX,231-offset,accentColor());
            badgeX+=measure(regular18_,"SD MOD").first+37;
        }
        if(item.hasConsole)pill("ON CONSOLE",badgeX,231-offset,LightText);
        int y=277;
        y+=flowText(bold28_,display.name,665,y-offset,488,White)+12;
        y+=flowText(regular18_,"Created by "+display.author,665,y-offset,488,Muted)+23;
        y+=flowText(regular18_,display.description,665,y-offset,488,LightText)+24;
        if(nativeSd&&item.hasSd&&item.hasConsole){
            text(bold20_,"SD copy takes priority",665,y-offset,accentColor(),488);
            y+=34;
            const std::string warningText="SD files override matching console files. Remove the console copy to avoid mixing versions.";
            y+=flowText(regular18_,warningText,665,y-offset,488,LightText)+24;
        }
        y+=flowText(regular18_,"Folder: "+item.folder,665,y-offset,488,Muted)+8;
        if(item.hasConsole)
            y+=flowText(regular18_,"Console storage: "+consoleStorage,665,y-offset,488,Muted)+8;
        if(!notice.empty())y+=flowText(regular18_,notice,665,y+12-offset,488,Green)+12;
        localModDetailsMaximumScroll_=std::max(0,y-(detailsClip.y+detailsClip.h));
    }else{
        wrapped(regular18_,std::string("Put each mod folder with its modload.txt manifest under ")+config::gameSdModsDisplayPath()+". Installed console mods are also listed here.",{665,247,548,150},Muted);
        if(!notice.empty())wrapped(regular18_,notice,{665,417,548,160},LightText);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);
    scrollBar(detailsClip,detailsScroll,localModDetailsMaximumScroll_);
    if(localModDetailsMaximumScroll_>0){
        const SDL_Rect up{1196,231,44,44},down{1196,562,44,44};
        quietPanel(up);quietPanel(down);
        directionIcon({1207,242,22,22},button::Up,LightText);
        directionIcon({1207,573,22,22},button::Down,LightText);
        touchTarget(up,TouchActionKind::Button,static_cast<int>(button::Left));
        touchTarget(down,TouchActionKind::Button,static_cast<int>(button::Right));
    }
    std::string primaryAction="Actions",deleteAction="Delete";
    if(!mods.empty()){
        const auto& item=mods[std::min(selected,mods.size()-1)];
        if(nativeSd&&item.hasSd&&item.hasConsole){
            primaryAction="Resolve";
            deleteAction="Delete console";
        }else if(item.hasSd){
            primaryAction="Copy to console";
            deleteAction=item.hasConsole?"Delete console":"Delete SD";
        }else if(item.hasConsole){
            primaryAction=nativeSd?"Move to SD":"Back up to SD";
            deleteAction="Delete console";
        }
    }
    navigationRail(3);
    if(mods.empty())suppressedTouchButtons_|=button::A|button::X;
    footer({{"A",primaryAction},{"D-PAD","Browse / read"},{"X/1",deleteAction},
            {"B","Back"}});
    present();
}

void Ui::workshopHeader(WorkshopTab tab){
    text(bold38_,"Workshop",244,116,White,500);
    text(regular18_,tab==WorkshopTab::Plugins?"The plugins that power your setup.":tab==WorkshopTab::CustomPackages?
         "Install ZIP packages from your SD card.":tab==WorkshopTab::Mods?"Download and manage mods for "+std::string(gameName(config::activeGame))+".":"Community-made maps for "+std::string(gameName(config::activeGame))+".",244,173,Muted,1008);
    const SDL_Rect tabs{476,122,776,48};
    roundRect(tabs,8,PanelBorder);
    roundRect({tabs.x+1,tabs.y+1,tabs.w-2,tabs.h-2},7,darkColor());
    const char* labels[]={"Plugins","Custom Maps","Mods","Custom Packages"};
    const int widths[]={180,210,140,246};int tabX=tabs.x;
    for(int i=0;i<4;++i){
        const SDL_Rect segment{tabX,tabs.y,widths[i],tabs.h};tabX+=widths[i];
        const bool active=int(tab)==i;
        if(active)roundRect({segment.x+4,segment.y+4,segment.w-8,segment.h-8},5,raisedColor());
        text(bold20_,labels[i],segment.x+segment.w/2,segment.y+10,
             active?accentColor():Muted,segment.w-24,true);
        touchTarget(segment,TouchActionKind::WorkshopTab,i);
    }
}

void Ui::localPackages(const std::string& directory,const std::vector<SdPackageEntry>& entries,
                       std::size_t selected,int scroll,const std::string& notice,bool resuming){
    std::string identity="local-packages|"+directory;
    for(const auto& entry:entries)identity+='|'+entry.name;
    pump();beginTouch(identity);backdrop();header("");navigationRail(2);workshopHeader(WorkshopTab::CustomPackages);
    const auto path="sd:/"+(directory.size()>std::string(SdBrowseRoot).size()?directory.substr(std::string(SdBrowseRoot).size()+1):"");
    text(regular18_,path,244,205,Muted,1008);
    const SDL_Rect clip{244,234,574,408};SDL_RenderSetClipRect(renderer_,&clip);
    for(std::size_t i=0;i<entries.size();++i){
        const SDL_Rect row{244,234+int(i)*66-scroll,574,58};
        if(row.y+row.h<clip.y||row.y>clip.y+clip.h)continue;
        quietPanel(row,i==selected);touchTarget(row,TouchActionKind::PackageEntry,int(i),&clip);
        auto* icon=navigationIcons_[entries[i].directory?3:1];const auto tint=i==selected?accentColor():Muted;
        SDL_SetTextureColorMod(icon,tint.r,tint.g,tint.b);const SDL_Rect symbol{258,row.y+14,28,28};SDL_RenderCopy(renderer_,icon,nullptr,&symbol);
        text(bold20_,entries[i].name,300,row.y+5,White,498);
        text(regular18_,entries[i].directory?"Folder":displayPackageSize(entries[i].bytes),300,row.y+31,Muted,498);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);scrollBar(clip,scroll,std::max(0,int(entries.size())*66-408));
    if(entries.empty())wrapped(regular22_,"No folders or ZIP packages here.",{270,290,520,110},LightText);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,848,239,848,643);
    text(bold28_,resuming?"Resume package":"Choose a package",878,238,White,364);
    wrapped(regular18_,"Browse your SD folders, then select a ZIP to review its files and destinations.",{878,291,364,92},LightText);
    text(bold20_,"ZIP folder destinations",878,404,White,364);
    text(regular18_,"update/  >  game update on USB / NAND",878,445,LightText,364);
    wrapped(regular18_,std::string("sd/  >  ")+config::gameSdContentDisplayPath(),{878,481,364,62},LightText);
    const SDL_Rect open{878,558,364,54};const bool ready=!entries.empty();
    primaryAction(open,ready&&entries[std::min(selected,entries.size()-1)].directory?"Open folder":"Review ZIP",true,ready);
    if(ready)touchTarget(open,TouchActionKind::PackageEntry,int(std::min(selected,entries.size()-1)));
    if(!notice.empty())text(regular18_,notice,878,620,Muted,364);
    else text(regular18_,"Your source ZIP stays on the SD card.",878,625,Muted,364);
    footer({{"A","Open / review"},{"UP/DN","Browse"},{"Y/2","Parent folder"},{"X/1","Refresh"},{"B",directory==SdBrowseRoot?"HQ":"Up a folder"}});present();
}

void Ui::workshopContent(WorkshopTab tab,const std::vector<MapPack>& maps,const std::vector<std::string>& installed,
                            std::size_t selected,int scrollOffset,const std::string& notice,bool installReady){
    const bool mods=tab==WorkshopTab::Mods;
    std::string identity=std::string(mods?"workshop-mods-":"workshop-custom-maps-")+(config::ghostsMode()?"ghosts":"bo2");
    for(const auto& map:maps)identity+='|'+map.id+'@'+map.full.version;
    pump();beginTouch(identity);
    backdrop();header("");navigationRail(2);workshopHeader(tab);
    if(!notice.empty())text(regular18_,notice,244,205,Muted,1008);
    if(!maps.empty()){
        const auto versionFor=[&](std::size_t i){return i<installed.size()?installed[i]:std::string{};};
        const auto stateFor=[&](std::size_t i){
            const auto version=versionFor(i);
            return version.empty()?std::string("Not installed"):
                compareVersions(version,maps[i].full.version)<0?std::string("Update available"):
                compareVersions(version,maps[i].full.version)>0?std::string("Newer version installed"):std::string("Installed");
        };
        const SDL_Rect clip{244,234,574,408};SDL_RenderSetClipRect(renderer_,&clip);
        for(std::size_t i=0;i<maps.size();++i){
            const SDL_Rect row{244,234+int(i)*142-scrollOffset,574,126};
            if(row.y+row.h<clip.y||row.y>clip.y+clip.h)continue;
            quietPanel(row,i==selected);touchTarget(row,TouchActionKind::CustomMapRow,int(i),&clip);
            roundRect({row.x+18,row.y+23,72,72},10,raisedColor());
            auto* icon=navigationIcons_[2];const auto accent=accentColor();SDL_SetTextureColorMod(icon,accent.r,accent.g,accent.b);
            const SDL_Rect symbol{row.x+34,row.y+39,40,40};SDL_RenderCopy(renderer_,icon,nullptr,&symbol);
            text(bold20_,maps[i].name,row.x+112,row.y+14,White,440);
            text(regular18_,"Version "+maps[i].full.version,row.x+112,row.y+46,Muted,440);
            const auto state=stateFor(i);const auto color=state=="Installed"?Green:state=="Update available"?accentColor():Muted;
            text(regular18_,state,row.x+112,row.y+82,color,440);
        }
        SDL_RenderSetClipRect(renderer_,nullptr);
        scrollBar(clip,scrollOffset,std::max(0,int(maps.size())*142-16-408));
        SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,848,239,848,643);
        const auto index=std::min(selected,maps.size()-1);const auto& map=maps[index];const auto version=versionFor(index);
        const auto* release=mapPackRelease(map,version,version==map.full.version);
        const std::string action=version==map.full.version?(mods?"Reinstall mod":"Reinstall map"):version.empty()?(mods?"Install mod":"Install map"):(mods?"Update mod":"Update map");
        text(bold28_,map.name,878,238,White,364);
        wrapped(regular18_,map.description,{878,288,364,126},LightText);
        text(regular18_,"Latest",878,436,Muted,130);text(regular18_,map.full.version,1016,436,LightText,226);
        text(regular18_,"Installed",878,473,Muted,130);text(regular18_,version.empty()?"None":version,1016,473,LightText,226);
        const auto bytes=release?release->compressedBytes:map.full.compressedBytes;
        text(regular18_,"Download: "+std::to_string((bytes+1048575)/1048576)+" MiB",878,512,Muted,364);
        const SDL_Rect install{878,558,364,54};primaryAction(install,action,true,installReady&&release);
        if(installReady&&release)touchTarget(install,TouchActionKind::CustomMapAction,int(index));
        text(regular18_,installReady?(mods?"ZIP folders choose SD / game storage":"Verified install to USB / internal storage"):"Check Install / Update before installing",878,625,Muted,364);
        footer({{"A","Review / install"},{"UP/DN","Navigate"},{"DPAD-LR","Switch tab"},{"X/1","Refresh"},{"B","HQ"}});present();return;
    }
    quietPanel({244,234,1008,408});
    roundRect({708,292,80,80},14,raisedColor());
    auto* icon=navigationIcons_[2];const auto accent=accentColor();
    SDL_SetTextureColorMod(icon,accent.r,accent.g,accent.b);
    const SDL_Rect symbol{728,312,40,40};SDL_RenderCopy(renderer_,icon,nullptr,&symbol);
    text(bold28_,mods?"No mods available yet":"No custom maps available yet",748,404,White,900,true);
    text(regular22_,"Check back for community releases.",748,455,LightText,900,true);
    text(regular18_,mods?"Installed mods can be managed from Mod Manager.":"Use the Plugins tab to manage your Aroma plugins.",748,512,Muted,900,true);
    suppressedTouchButtons_|=button::A;
    footer({{"DPAD-LR","Switch tab"},{"X/1","Refresh"},{"B","HQ"}});present();
}

void Ui::pluginCatalog(const std::vector<WorkshopPlugin>& plugins,
                       const std::vector<PluginInstallStatus>& statuses,
                       const std::vector<bool>& checked,
                       std::size_t selected,int scrollOffset,
                       const std::string& catalogNotice,const std::vector<bool>& releaseReady){
    std::string touchIdentity="plugin-catalog";
    for(const auto& plugin:plugins)touchIdentity+='|'+plugin.key;
    pump();beginTouch(touchIdentity);backdrop();header("");navigationRail(2);
    workshopHeader(WorkshopTab::Plugins);
    if(!catalogNotice.empty())text(regular18_,catalogNotice,244,205,Muted,1008);
    const SDL_Rect clip{244,234,574,408};SDL_RenderSetClipRect(renderer_,&clip);
    const auto stateFor=[&](std::size_t i){
        std::pair<std::string,SDL_Color> state{"Open to check",Muted};
        if(i>=statuses.size()||i>=checked.size()||!checked[i])return state;
        const auto& status=statuses[i];
        state={"Not installed",Muted};
        if(status.presence==PluginPresence::Managed){
            const bool updateKnown=i<releaseReady.size()&&releaseReady[i]&&status.updateAvailable;
            state={updateKnown?"Update available":"Installed",updateKnown?accentColor():Green};
        }else if(status.presence==PluginPresence::Unmanaged)state={"Manual install",accentColor()};
        else if(status.presence==PluginPresence::Damaged)state={status.repairable?"Repair needed":"Manual recovery",Red};
        return state;
    };
    for(std::size_t i=0;i<plugins.size();++i){
        const SDL_Rect row{244,234+int(i)*142-scrollOffset,574,126};
        if(row.y+row.h<clip.y||row.y>clip.y+clip.h)continue;
        quietPanel(row,i==selected);
        touchTarget(row,TouchActionKind::PluginRow,int(i),&clip);
        const SDL_Rect art{row.x+14,row.y+20,80,80};
        SDL_RenderCopy(renderer_,pluginArtwork(plugins[i]),nullptr,&art);
        text(bold20_,plugins[i].name,row.x+112,row.y+14,White,440);
        text(regular18_,"by "+plugins[i].creator,row.x+112,row.y+46,Muted,440);
        const auto state=stateFor(i);
        if(state.second.g==Green.g)checkmark({row.x+112,row.y+88,16,16},Green);
        text(regular18_,state.first,row.x+112+(state.second.g==Green.g?26:0),row.y+82,state.second,414);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);
    pluginCatalogMaximumScroll_=std::max(0,int(plugins.size())*142-16-408);
    scrollBar(clip,scrollOffset,pluginCatalogMaximumScroll_);
    SDL_SetRenderDrawColor(renderer_,43,49,53,255);SDL_RenderDrawLine(renderer_,848,239,848,643);
    if(plugins.empty()){
        wrapped(regular22_,"No plugin listings are available. Try refreshing the Workshop.",{244,274,574,144},LightText);
    }else{
        const std::size_t i=std::min(selected,plugins.size()-1);
        const auto& plugin=plugins[i];
        text(bold28_,plugin.name,878,238,White,364);
        wrapped(regular18_,plugin.description,{878,288,364,126},LightText);
        const auto state=stateFor(i);
        text(regular18_,"Status",878,436,Muted,130);
        text(regular18_,state.first,1016,436,state.second,226);
        if(i<statuses.size()&&!statuses[i].installedVersion.empty()){
            text(regular18_,"Installed",878,473,Muted,130);
            text(regular18_,statuses[i].installedVersion,1016,473,LightText,226);
        }
        if(i<statuses.size()&&i<checked.size()&&checked[i]&&statuses[i].presence==PluginPresence::Managed&&
           (i>=releaseReady.size()||!releaseReady[i]))
            text(regular18_,"Latest release check unavailable",878,512,Muted,364);
        const SDL_Rect open{878,558,364,54};
        primaryAction(open,"Manage plugin",!navigationFocused_);
        touchTarget(open,TouchActionKind::PluginRow,int(i));
        text(regular18_,"Review releases and auto-update settings",878,625,Muted,364);
    }
    footer({{"A","Open plugin"},{"UP/DN","Navigate"},{"DPAD-LR","Switch tab"},{"X/1","Refresh"},{"B","HQ"}});present();
}

void Ui::pluginDetails(const WorkshopPlugin& plugin,const PluginRelease* release,
                       const PluginInstallStatus& status,bool autoUpdateEnabled,
                       int selected,int scrollOffset,const std::string& notice,
                       const std::string& feedStatus){
    pump();beginTouch("plugin|"+plugin.key+"|"+(release?release->version:"unavailable")+
        "|"+std::to_string(static_cast<int>(status.presence)));backdrop();
    SDL_Rect clip{244,118,996,456};SDL_RenderSetClipRect(renderer_,&clip);
    const int top=122-scrollOffset;
    const int titleHeight=flowDocument(bold28_,plugin.name,512,White).height;
    const int authorHeight=flowDocument(regular18_,"Published by "+plugin.creator,512,Muted).height;
    const int descriptionHeight=flowDocument(regular18_,plugin.description,512,LightText).height;
    const int descriptionY=top+22+titleHeight+10+authorHeight+22;
    const int heroHeight=std::max(240,descriptionY-top+descriptionHeight+24);
    quietPanel({244,top,1008,heroHeight});
    SDL_Rect art{264,top+22,148,148};SDL_RenderCopy(renderer_,pluginArtwork(plugin),nullptr,&art);
    flowText(bold28_,plugin.name,432,top+22,512,White);
    flowText(regular18_,"Published by "+plugin.creator,432,top+22+titleHeight+10,512,Muted);
    flowText(regular18_,plugin.description,432,descriptionY,512,LightText);
    const bool cachedRelease=feedStatus.rfind("Using cached",0)==0||
        feedStatus.find("retained cached")!=std::string::npos||
        feedStatus.find("retained the cached")!=std::string::npos;
    std::string installed=status.installedVersion.empty()?"Not installed":status.installedVersion;
    std::string state="Not installed";SDL_Color stateColor=Muted;
    if(status.presence==PluginPresence::Managed){
        if(release&&release->revision>status.installedRevision){state="Update available";stateColor=accentColor();}
        else if(release&&status.installedRevision==release->revision&&status.installedVersion==release->version){state=cachedRelease?"Matches cached release":"Current";stateColor=cachedRelease?LightText:Green;}
        else if(!release){state="Installed";stateColor=LightText;}
        else {state="Installed (different release)";stateColor=LightText;}
    }
    else if(status.presence==PluginPresence::Unmanaged){state="Manual install detected";stateColor=accentColor();}
    else if(status.presence==PluginPresence::Damaged){state=status.repairable?"Repair needed":"Manual recovery required";stateColor=Red;}
    text(regular18_,"Status",984,top+22,Muted);flowText(regular18_,state,984,top+48,244,stateColor);
    text(regular18_,"Installed",984,top+105,Muted);text(bold20_,installed,984,top+129,White,244);
    text(regular18_,cachedRelease?"Cached release":"Latest",984,top+167,Muted);text(bold20_,release?release->version:"Check unavailable",984,top+191,release?LightText:Muted,244);
    int y=top+heroHeight+28;text(bold20_,"Managed Aroma files",268,y,White);y+=37;
    for(const auto& path:plugin.managedPaths)y+=flowText(regular18_,"sd:/"+path,268,y,960,LightText)+8;
    y+=16;text(bold20_,"Restart after installing",268,y,accentColor());y+=35;
    std::string restartNote="A full console/Aroma restart is required after installing or updating this plugin.";
    if(plugin.key=="PROTARIUM")
        restartNote+=" Protarium/Inkay also changes network and SSL behavior while active; review the publisher's safety notes before installing.";
    else if(plugin.key=="UNBOUND_MOD_LINK")
        restartNote+=" Unbound Mod Link performs no downloads while a game is running and modifies no installed game files.";
    else if(plugin.key=="GAMEPAD_MIC_REDIRECT")
        restartNote+=" GamePad Mic Redirect changes microphone routing for compatible games; review its publisher page for current controls and testing notes.";
    y+=flowText(regular18_,restartNote,268,y,960,LightText)+28;
    if(!feedStatus.empty()){text(bold20_,"Release check",268,y,White);y+=35;y+=flowText(regular18_,feedStatus,268,y,960,release&&!cachedRelease?Muted:accentColor())+28;}
    if(!notice.empty()){text(bold20_,"Status message",268,y,White);y+=35;y+=flowText(regular18_,notice,268,y,960,LightText)+28;}
    pluginDetailsMaximumScroll_=std::max(0,y+scrollOffset-574);
    SDL_RenderSetClipRect(renderer_,nullptr);header("Workshop plugin");SDL_Rect actionBar{244,588,1008,70};SDL_SetRenderDrawColor(renderer_,13,18,22,255);SDL_RenderFillRect(renderer_,&actionBar);
    std::string installLabel="INSTALL";if(status.presence==PluginPresence::Managed)installLabel=release&&release->revision>status.installedRevision?"UPDATE":"REINSTALL";else if(status.presence==PluginPresence::Unmanaged)installLabel="REPLACE & MANAGE";else if(status.presence==PluginPresence::Damaged)installLabel=status.repairable?"REPAIR":"RECOVERY INFO";
    const bool startupAutoUpdateSupported=supportsStartupPluginAutoUpdate(plugin);
    const std::string updateLabel=!startupAutoUpdateSupported
        ? "AUTO-UPDATE UNAVAILABLE"
        : plugin.key=="UNBOUND_MOD_LINK"?"AUTO-UPDATE MANAGED":
          std::string("AUTO-UPDATE ")+(autoUpdateEnabled?"ON":"OFF");
    const std::vector<std::pair<std::string,SDL_Color>> actions={{installLabel,accentColor()},
        {updateLabel,!startupAutoUpdateSupported?PanelBorder:(autoUpdateEnabled?Green:Blue)}};
    for(int i=0;i<2;++i){
        SDL_Rect buttonRect{254+i*502,599,476,47};quietPanel(buttonRect,i==selected);
        const bool enabled=i==0?(release||status.presence==PluginPresence::Damaged):startupAutoUpdateSupported;
        text(bold20_,actions[i].first,buttonRect.x+buttonRect.w/2,buttonRect.y+9,
             enabled?(i==selected?accentColor():LightText):Muted,440,true);
        if(enabled)touchTarget(buttonRect,TouchActionKind::PluginAction,i);
    }
    if((selected==0&&!release&&status.presence!=PluginPresence::Damaged)||
       (selected==1&&!startupAutoUpdateSupported))suppressedTouchButtons_|=button::A;
    scrollBar(clip,scrollOffset,pluginDetailsMaximumScroll_);
    navigationRail(2);
    footer({{"A","Choose"},{"UP/DN","Scroll"},{"DPAD-LR","Action"},{"B","Back"}});present();
}
void Ui::workshopCatalog(const std::vector<WorkshopMod>& mods,std::size_t featured,std::size_t selected,int scrollOffset,bool featuredFocus){
    pump();SDL_SetRenderDrawColor(renderer_,Steam.r,Steam.g,Steam.b,255);SDL_RenderClear(renderer_);SDL_Rect clip{0,102,W,556};SDL_RenderSetClipRect(renderer_,&clip);const int top=118-scrollOffset;text(bold28_,"Featured & Trending",30,top,White);text(regular18_,"Community picks - use Left and Right to browse",30,top+37,{166,183,196,255});
    if(!mods.empty()){const std::size_t count=std::min<std::size_t>(4,mods.size());std::size_t start=featured>1?featured-1:0;if(start+count>mods.size())start=mods.size()-count;for(std::size_t slot=0;slot<count;++slot){const std::size_t index=start+slot;SDL_Rect card{30+int(slot)*306,top+72,286,152};roundRect(card,10,index==featured&&featuredFocus?accentColor():SteamCard);SDL_Rect art{card.x+8,card.y+8,104,104};SDL_RenderCopy(renderer_,squareLogo_,nullptr,&art);text(bold20_,mods[index].name,card.x+124,card.y+14,White,150);text(regular18_,"by "+mods[index].creator,card.x+124,card.y+45,{166,183,196,255},150);text(bold20_,stars(mods[index]),card.x+124,card.y+81,Blue,150);text(regular18_,std::to_string(mods[index].commentCount)+" comments",card.x+124,card.y+114,{166,183,196,255},150);}}
    const int listTop=top+258;text(bold28_,"Browse all mods",30,listTop,White);
    for(std::size_t i=0;i<mods.size();++i){const int col=int(i%2),row=int(i/2);SDL_Rect card{30+col*615,listTop+48+row*116,595,104};if(card.y+card.h<102||card.y>658)continue;roundRect(card,8,i==selected&&!featuredFocus?Blue:SteamCard);SDL_Rect art{card.x+7,card.y+7,90,90};SDL_RenderCopy(renderer_,squareLogo_,nullptr,&art);text(bold20_,mods[i].name,card.x+112,card.y+9,White,300);text(regular18_,"by "+mods[i].creator,card.x+112,card.y+36,{166,183,196,255},300);text(regular18_,std::to_string(mods[i].commentCount)+" comments",card.x+112,card.y+66,{166,183,196,255},180);text(bold20_,stars(mods[i]),card.x+420,card.y+10,Blue,150);text(regular18_,std::to_string(mods[i].upvotes+mods[i].downvotes)+" ratings",card.x+420,card.y+44,{166,183,196,255},150);}
    SDL_RenderSetClipRect(renderer_,nullptr);header("Community Workshop");footer({{"A","Open item"},{"B","HQ"},{"UP/DN","Scroll"},{"L/R","Move"}});present();
}
void Ui::modDetails(const WorkshopMod& mod,const std::vector<Review>& reviews,int selected,int scrollOffset,const std::string& notice,const std::string& userName){
    pump();SDL_SetRenderDrawColor(renderer_,Steam.r,Steam.g,Steam.b,255);SDL_RenderClear(renderer_);SDL_Rect clip{0,102,W,486};SDL_RenderSetClipRect(renderer_,&clip);const int top=122-scrollOffset;SDL_Rect hero{30,top,1220,224};roundRect(hero,12,SteamCard);SDL_Rect art{48,top+18,188,188};SDL_RenderCopy(renderer_,squareLogo_,nullptr,&art);text(bold38_,mod.name,265,top+18,White,620);text(regular22_,"Created by "+mod.creator,265,top+69,{166,183,196,255},620);text(bold20_,stars(mod),945,top+22,Blue,240);text(regular18_,std::to_string(mod.upvotes)+" positive / "+std::to_string(mod.downvotes)+" negative",945,top+58,{166,183,196,255},260);if(!notice.empty())text(regular18_,notice,945,top+101,Green,260);wrapped(regular22_,mod.description,{265,top+120,650,85},White);
    const int commentsTop=top+260;text(bold28_,"Comments",40,commentsTop,White);text(regular18_,"Posting as "+userName,190,commentsTop+7,{166,183,196,255});if(reviews.empty())text(regular22_,"No comments yet. Be the first to leave one.",40,commentsTop+54,{166,183,196,255});
    for(std::size_t i=0;i<reviews.size();++i){SDL_Rect card{40,commentsTop+50+int(i)*112,1200,98};if(card.y+card.h<102||card.y>588)continue;roundRect(card,8,SteamCard);circle(card.x+35,card.y+35,22,i%2?accentColor():Blue);const std::string initial=reviews[i].author.empty()?"?":reviews[i].author.substr(0,1);text(bold20_,initial,card.x+35,card.y+19,White,0,true);text(bold20_,reviews[i].author,card.x+72,card.y+13,White,240);wrapped(regular18_,reviews[i].body,{card.x+72,card.y+43,1085,43},{205,215,222,255});}
    SDL_RenderSetClipRect(renderer_,nullptr);header("Workshop item");SDL_Rect actionBar{0,588,W,70};SDL_SetRenderDrawColor(renderer_,13,18,22,255);SDL_RenderFillRect(renderer_,&actionBar);const std::vector<std::pair<std::string,SDL_Color>> actions={{"DOWNLOAD",accentColor()},{"THUMBS UP",Green},{"THUMBS DOWN",Red},{"COMMENT",Blue}};for(int i=0;i<4;++i){SDL_Rect button{24+i*313,599,291,47};roundRect(button,8,i==selected?actions[i].second:SteamCard);text(bold20_,actions[i].first,button.x+button.w/2,button.y+9,White,0,true);}footer({{"A","Choose"},{"B","Back"},{"UP/DN","Scroll"},{"L/R","Action"}});present();
}
void Ui::commentEditor(const WorkshopMod& mod,const std::string& draft,int selectedRow,int selectedColumn){
    pump();backdrop();header("Write a comment");text(bold28_,"Review "+mod.name,35,119,White,900);text(regular18_,"Use the D-Pad and A. Comments can be up to 240 characters.",35,156,LightText);SDL_Rect entry{35,195,1210,88};roundRect(entry,12,PanelBorder);SDL_Rect entryInner{38,198,1204,82};roundRect(entryInner,10,panelColor());wrapped(regular22_,draft.empty()?"Your comment will appear here...":draft,{58,216,1160,50},draft.empty()?Muted:White);text(regular18_,std::to_string(draft.size())+" / 240",1135,255,Muted);
    for(int row=0;row<int(KeyboardRows.size());++row){const auto& keys=KeyboardRows[row];const int keyWidth=row==3?132:72,gap=9,total=int(keys.size())*keyWidth+(int(keys.size())-1)*gap,x=(W-total)/2,y=310+row*72;for(int col=0;col<int(keys.size());++col){SDL_Rect key{x+col*(keyWidth+gap),y,keyWidth,54};const bool active=row==selectedRow&&col==selectedColumn;if(active){SDL_Rect halo{key.x-4,key.y-4,key.w+8,key.h+8};roundRect(halo,12,accentColor());}roundRect(key,8,active?SDL_Color{74,34,15,255}:raisedColor());text(row==3?regular18_:bold20_,keys[col],key.x+key.w/2,key.y+12,active?White:LightText,0,true);}}
    footer({{"A","Type / choose"},{"B","Cancel"},{"D-PAD","Move"},{"+","Exit"}});present();
}
} // namespace unbound

namespace unbound {
void Ui::networkSummary(bool focused){
    const SDL_Rect panel{244,550,1008,100};
    quietPanel(panel,focused);
    touchTarget(panel,TouchActionKind::HomeAction,5);
    networkGlobe_.draw(renderer_,310,600,40,globeYaw_,globePitch_,networkStats_.cells,
                       config::ghostsMode()?accentColor():SDL_Color{255,105,18,255});
    text(bold20_,"UNC Network",378,565,accentColor(),250);
    text(regular18_,networkStats_.stale?"Last known activity  >":"Explore the globe  >",378,609,Muted,265);
    const auto stat=[&](unsigned value,const char* label,int x,int width){
        text(bold28_,networkStats_.available?std::to_string(value):"--",x,562,White,width);
        text(regular18_,label,x,609,Muted,width);
    };
    if(config::ghostsMode()){
        stat(networkStats_.players,"Reported players",660,245);
        stat(networkStats_.servers,"Advertised lobbies",940,292);
    }else{
        stat(networkStats_.players,"Players online",660,170);
        stat(networkStats_.servers,"Servers",846,150);
        stat(networkStats_.unique,"Unique today (UTC)",1008,224);
    }
}
void Ui::network(){
 pump();beginTouch("network");backdrop();header("Network",true);
 text(bold28_,config::ghostsMode()?"Ghosts network":"The Unbound community",244,125,White,1008);
 const unsigned age=(SDL_GetTicks()-networkStats_.received)/1000;
 text(regular18_,!networkStats_.available?(networkStats_.stale?"Network activity is unavailable. HQ remains ready to use.":"Connecting to the Unbound network..."):networkStats_.stale?"Showing last known activity. Reconnecting automatically.":"Updated "+std::to_string(age)+"s ago",244,168,Muted,1008);
 quietPanel({244,210,1008,94});
 const auto stat=[&](unsigned n,const char* label,int x){text(bold28_,networkStats_.available?std::to_string(n):"--",x,220,White,270);text(regular18_,label,x,263,Muted,270);};
 stat(networkStats_.players,config::ghostsMode()?"Host-reported players":"Players online",274);
 stat(networkStats_.servers,config::ghostsMode()?"Advertised lobbies":"Active servers",610);
 if(!config::ghostsMode())stat(networkStats_.unique,"Unique today (UTC)",940);
 quietPanel({244,324,620,322});networkGlobe_.draw(renderer_,554,477,137,globeYaw_,globePitch_,networkStats_.cells,
                    config::ghostsMode()?accentColor():SDL_Color{255,105,18,255});
 text(regular18_,networkStats_.heatAvailable?(networkStats_.heatStale?"Last known regional activity":(config::ghostsMode()?"Approximate lobby regions":"Approximate player regions")):"Regional activity unavailable",264,613,Muted,580);
 quietPanel({886,324,366,322});
 if(config::ghostsMode()){
     text(bold20_,"Lobby activity",906,345,White,320);
     wrapped(regular18_,"Player counts are reported by lobby hosts. The globe shows approximate lobby activity by country.",
             {906,388,320,160},Muted);
 }else{
     text(bold20_,"Across the network",906,345,White,320);
     const char* names[]={"PC","Wii U","Switch","Android","Other"};
     for(int i=0;i<5;++i){text(regular18_,names[i],906,388+i*33,Muted,220);text(regular18_,networkStats_.available?std::to_string(networkStats_.platforms[i]):"--",1170,388+i*33,LightText,60);}
     text(regular18_,"Menus, lobbies and matches",906,556,Muted,320);
 }
 navigationRail(5);footer({{"D-PAD","Rotate / drag globe"},{"B","Back"}});present();
}
}

namespace unbound {
void Ui::quickUpdate(const GameInstall& install,const std::vector<QuickUpdateRow>& rows,bool coreCurrent,int scroll){
    (void)coreCurrent;
    pump();beginTouch("quick-update-confirm");backdrop();header("");
    quietPanel({136,134,1008,480});
    text(bold28_,"Updates available",164,154,White,952);
    text(regular18_,"Update your installed content now? One confirmation updates everything listed below.",164,200,LightText,952);
    const SDL_Rect clip{164,247,952,229};SDL_RenderSetClipRect(renderer_,&clip);
    std::uint64_t bytes=0,download=0,installTime=0;bool measured=true;
    for(std::size_t i=0;i<rows.size();++i){
        const auto& row=rows[i];bytes+=row.networkBytes.value_or(row.bytes);download+=row.downloadSeconds;installTime+=row.installSeconds;measured=measured&&row.timingFromHistory;
        const int y=247+int(i)*88-scroll;if(y+80<clip.y||y>clip.y+clip.h)continue;
        roundRect({164,y,952,80},7,raisedColor());
        text(bold20_,row.title,182,y+8,White,590);
        const auto size=displayPackageSize(row.networkBytes.value_or(row.bytes));
        text(regular18_,size,1098-measure(regular18_,size).first,y+10,Muted,220);
        text(regular18_,row.installed+"  >  "+row.latest,182,y+43,Muted,300);
        const auto timing=std::string(row.networkBytes.value_or(row.bytes)?"Download ~"+updateDuration(row.downloadSeconds):"Saved ZIP")+
            "  /  Install ~"+updateDuration(row.installSeconds);
        text(regular18_,timing,500,y+43,LightText,598);
    }
    SDL_RenderSetClipRect(renderer_,nullptr);scrollBar(clip,scroll,quickUpdateMaximumScroll(rows.size()));
    text(regular18_,"Download "+displayPackageSize(bytes)+"  /  Total ~"+updateDuration(download+installTime),164,491,White,952);
    text(regular18_,measured?"Estimated from this console's previous downloads and installs.":"Initial estimates improve as HQ records downloads and installs.",164,521,Muted,590);
    text(regular18_,std::string(deviceName(install.update.device))+" / "+displayLanguage(install.languageFolder),164,553,LightText,555);
    const SDL_Rect accept{776,541,340,52};primaryAction(accept,"Update all",true,!rows.empty());
    if(!rows.empty())touchTarget(accept,TouchActionKind::Button,int(button::A));
    text(regular18_,"B  Not now",164,582,Muted,380);touchTarget({154,575,360,34},TouchActionKind::Button,int(button::B));
    if(rows.size()>2)footer({{"A","Update all"},{"UP/DN","Scroll"},{"B","Not now"},{"X/1","Install settings"}});
    else footer({{"A","Update all"},{"B","Not now"},{"X/1","Install settings"}});
    present();
}
}
