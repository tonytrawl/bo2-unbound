// Preview-only member implementation, compiled with the production UI renderer.
namespace unbound {
void Ui::installLayoutPreview(int option){
    pump();beginTouch("layout-preview");
    SDL_SetRenderDrawColor(renderer_,12,15,17,255);SDL_RenderClear(renderer_);
    header("Install / Update");
    const auto panel=[&](int x,int y,int w,int h){roundRect({x,y,w,h},10,PanelBorder);roundRect({x+1,y+1,w-2,h-2},9,Panel);};
    const auto label=[&](const std::string& s,int x,int y){text(regular18_,s,x,y,Muted,900);};
    const auto button=[&](const std::string& s,int x,int y,int w){roundRect({x,y,w,54},8,Orange);text(bold20_,s,x+22,y+12,White,w-40);};
    if(option==0){
        text(bold38_,"Ready to update",80,130,White,1100);
        label("Everything is detected. Review your destination, then install.",80,186);
        panel(80,238,1120,270);
        label("UNBOUND RELEASE",112,266);text(bold28_,"Beta 3",112,300,White,430);
        label("Update found on your SD card",112,354);
        label("Verified before installation. No download needed if valid.",112,390);
        label("INSTALL TO",760,266);text(bold20_,"USB storage",760,300,White,370);
        label("USA  /  English",760,342);label("Change storage or language  >",760,402);
        button("Verify & install from SD",80,540,480);
        label("Use internet instead",610,556);
    }else if(option==1){
        label("1  REVIEW     /     2  VERIFY     /     3  INSTALL",80,134);
        text(bold38_,"Choose your update source",80,178,White,1100);
        panel(80,256,546,246);panel(646,256,554,246);
        text(bold28_,"SD card",110,286,White,460);label("Package found",110,334);
        label("Use the ZIP you downloaded on your PC.",110,376);
        button("Use SD package",110,424,480);
        text(bold28_,"Internet",676,286,White,460);label("Download through HQ",676,334);
        label("Resume saved progress where available.",676,376);
        label("Download instead  >",676,440);
        label("Destination: USB storage  /  English                         Change destination  >",80,546);
        label("Both sources receive the same integrity checks.",80,588);
    }else{
        text(bold38_,"Install Unbound",80,130,White,1100);
        panel(80,210,690,394);panel(794,210,406,394);
        label("PACKAGE",110,236);text(bold28_,"Beta 3 update",110,272,White,620);
        label("SOURCE",110,336);text(bold20_,"SD card package found",110,368,White,620);
        label("Change source  >",110,412);
        label("HQ checks the ZIP before installing.",110,472);
        label("If interrupted, return here to resume.",110,506);
        label("DESTINATION",824,236);text(bold20_,"USB storage",824,272,White,346);
        label("Language: English",824,318);label("Region: USA",824,354);
        label("Change settings  >",824,410);
        button("Install from SD",824,520,346);
    }
    footer({{"A","Select"},{"D-PAD","Navigate"},{"B","Back"}});present();
}
}

