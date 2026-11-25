// Desktop concept renders only. No feed or production navigation changes.
namespace unbound {
void Ui::installLayoutPreview(int option){
    pump();beginTouch("network-concept");
    SDL_SetRenderDrawColor(renderer_,12,15,17,255);SDL_RenderClear(renderer_);
    const bool detail=option%2, cinematic=option>=2;
    header(detail?"Network":"Unbound HQ");
    const auto panel=[&](SDL_Rect r){roundRect(r,9,PanelBorder);roundRect({r.x+1,r.y+1,r.w-2,r.h-2},8,Panel);};
    const auto label=[&](std::string s,int x,int y,int w=900){text(regular18_,s,x,y,Muted,w);};
    const auto stat=[&](std::string n,std::string s,int x,int y){text(bold28_,n,x,y,White,220);label(s,x,y+39,220);};
    const auto globe=[&](int cx,int cy,int radius){
        SDL_Surface* raw=IMG_Load("/work/../unc-website/public/globe-map.webp");
        if(!raw)raw=IMG_Load("/tmp/globe-map.webp");
        SDL_Surface* map=raw?SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_RGBA32,0):nullptr;
        if(raw)SDL_FreeSurface(raw);
        SDL_Surface* out=SDL_CreateRGBSurfaceWithFormat(0,radius*2,radius*2,32,SDL_PIXELFORMAT_RGBA32);
        SDL_FillRect(out,nullptr,0);
        for(int y=0;y<radius*2;++y)for(int x=0;x<radius*2;++x){
            const double nx=(x-radius+.5)/radius,ny=(radius-y-.5)/radius,rr=nx*nx+ny*ny;
            if(rr>1)continue;
            const double z=std::sqrt(1-rr),lat=std::asin(ny),lon=std::atan2(nx,z)-0.35;
            Uint8 r=22,g=36,b=42,a=255;
            if(map){int mx=int((lon/(2*3.14159265)+.5)*map->w)%map->w;mx=(mx+map->w)%map->w;
                int my=std::clamp(int((.5-lat/3.14159265)*map->h),0,map->h-1);
                auto pixel=*reinterpret_cast<Uint32*>(static_cast<Uint8*>(map->pixels)+my*map->pitch+mx*4);
                SDL_GetRGBA(pixel,map->format,&r,&g,&b,&a);
                float light=.24f+.62f*float(z);r=Uint8(r*light);g=Uint8(g*light);b=Uint8(b*light);
            }
            auto* dst=reinterpret_cast<Uint32*>(static_cast<Uint8*>(out->pixels)+y*out->pitch+x*4);
            *dst=SDL_MapRGBA(out->format,r,g,b,255);
        }
        SDL_Texture* t=SDL_CreateTextureFromSurface(renderer_,out);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
        SDL_Rect dest{cx-radius,cy-radius,radius*2,radius*2};SDL_RenderCopy(renderer_,t,nullptr,&dest);
        SDL_DestroyTexture(t);SDL_FreeSurface(out);if(map)SDL_FreeSurface(map);
        // Synthetic coarse regional activity, not live player positions.
        for(const auto& point:std::vector<std::pair<double,double>>{{40,-75},{50,-5},{50,15},{30,30},{-20,-45},{10,10},{55,35},{35,-5}}){
            const double lat=point.first*3.14159265/180,lon=point.second*3.14159265/180+.35;
            if(std::cos(lat)*std::cos(lon)<=0)continue;
            int x=cx+int(radius*std::cos(lat)*std::sin(lon)),y=cy-int(radius*std::sin(lat));
            roundRect({x-7,y-7,14,14},7,SDL_Color{87,40,13,230});roundRect({x-3,y-3,6,6},3,Orange);
        }
    };
    // Proposed dedicated tab in the existing navigation rail.
    const std::array<const char*,6> tabs{{"HQ","Install / Update","Workshop","Mod Manager","Network","Support"}};
    for(int i=0;i<6;++i){int y=154+i*74;bool chosen=detail?i==4:i==0;
        if(chosen){roundRect({15,y-8,196,54},6,PanelRaised);roundRect({15,y-8,3,54},1,Orange);}
        text(regular18_,tabs[i],32,y+7,chosen?White:Muted,175);
    }
    if(!detail){
        panel({244,122,620,348});label("LATEST RELEASE",266,141);text(bold28_,"What's new",266,170,White,570);
        SDL_Rect art{266,218,576,154};SDL_RenderCopy(renderer_,boot_,nullptr,&art);
        text(bold20_,"Beta 3 is here",266,389,White,570);label("Read the latest news and release notes  >",266,427,570);
        panel({886,122,366,348});text(bold20_,"Your Black Ops II",908,144,White,320);
        const std::array<const char*,6> rows{{"Game: Game disc","Update: USB storage","Official: v128 / current","Region: USA","Language: English","Unbound: 0.2.0"}};
        for(int i=0;i<6;++i)label(rows[i],908,191+i*31,320);
        if(!cinematic){
            panel({244,488,1008,100});globe(430,538,35);text(bold20_,"UNC",264,507,Orange,140);
            label("Network  >",264,547,140);
            stat("124","Players online",482,500);stat("18","Servers",720,500);stat("862","Unique today (UTC)",930,500);
        }else{
            panel({244,488,1008,120});globe(310,548,46);
            text(bold20_,"UNC NETWORK",378,505,Orange,240);label("Explore the globe  >",378,549,250);
            stat("124","Online",660,504);stat("18","Servers",826,504);stat("862","Today (UTC)",1000,504);
        }
        roundRect({886,cinematic?616:604,366,42},6,Orange);text(bold20_,"Launch Game",980,cinematic?623:611,White,240);
        label("SAMPLE DATA / LAYOUT PREVIEW",244,cinematic?626:618,600);
    }else if(!cinematic){
        text(bold28_,"The Unbound community",244,125,White,1000);label("SAMPLE DATA  /  Stats refreshed 12s ago",244,166);
        panel({244,210,1008,94});stat("124","Players online",274,220);stat("18","Active servers",610,220);stat("862","Unique today (UTC)",940,220);
        panel({244,324,620,322});globe(554,477,137);label("Approximate regional activity / 15-minute snapshot",264,615,580);
        panel({886,324,366,322});text(bold20_,"Across the network",906,345,White,320);
        label("PC                              64",906,394,320);label("Wii U                          32",906,430,320);label("Switch                        20",906,466,320);label("Android                        8",906,502,320);
        text(bold20_,"Browse 18 servers  >",906,564,Orange,320);
        label("Menus, lobbies and matches included",906,605,320);
    }else{
        panel({244,122,1008,524});text(bold28_,"Connected around the world",272,146,White,940);
        label("SAMPLE DATA / Regional activity, not exact player locations",272,190,930);
        globe(575,425,185);
        stat("124","Players online",1004,264);stat("18","Active servers",1004,366);stat("862","Unique today (UTC)",1004,468);
        label("Globe updated 4m ago",272,612,460);text(bold20_,"Browse servers  >",1004,590,Orange,225);
    }
    footer({{"A","Select"},{"D-PAD","Navigate"},{"B","Back"}});present();
}
}
