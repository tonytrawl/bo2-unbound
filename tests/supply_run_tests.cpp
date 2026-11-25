#include "supply_run.hpp"
#include "supply_run_display.hpp"
#include <cmath>
#include <cassert>
#include <iostream>

using unbound::SupplyRun;

int main(){
    unbound::SupplyRunDisplay display;
    assert(display.enabled()&&display.amount(0)==1.0f);
    display.toggle(100);
    assert(!display.enabled()&&display.amount(100)==1.0f);
    assert(std::fabs(display.amount(250)-.5f)<.0001f);
    display.toggle(250);
    assert(display.enabled()&&std::fabs(display.amount(250)-.5f)<.0001f);
    assert(display.amount(400)>.5f&&display.amount(400)<1);
    assert(display.amount(550)==1&&!display.transitioning(550));
    display.restore(false);
    assert(!display.enabled()&&display.amount(1000)==0);
    display.toggle(0xfffffff0u);
    assert(display.transitioning(20)&&display.amount(20)>0&&display.amount(20)<1);
    assert(display.amount(0xfffffff0u+300u)==1);

    SupplyRun game;
    unsigned now=0;
    game.queueJump();game.update(now+=33);
    assert(game.height()>0);
    const float previous=game.distance();
    game.update(now+=30000);
    // A blocked/slow display cannot advance the simulation by the stall time.
    assert(game.distance()-previous<40);
    while(!game.crashed()&&now<40000)game.update(now+=33);
    assert(game.crashed());
    const auto best=game.best();
    const auto stopped=game.distance();
    game.update(now+=1000);
    assert(game.distance()==stopped);
    game.queueJump();game.update(now+=33);
    assert(!game.crashed()&&game.score()==0&&game.best()==best);

    // Duck is a held action; it blocks jumping and does not avoid ground hazards.
    SupplyRun crouch;crouch.setDucking(true);crouch.queueJump();crouch.update(33);
    assert(crouch.ducking()&&crouch.height()==0);
    crouch.setDucking(false);crouch.queueJump();crouch.update(66);
    assert(!crouch.ducking()&&crouch.height()>0);
    SupplyRun alwaysDuck;alwaysDuck.setDucking(true);
    for(unsigned tick=33;tick<2500&&!alwaysDuck.crashed();tick+=33)alwaysDuck.update(tick);
    assert(alwaysDuck.crashed());

    // Alternate jumps and held ducking through every obstacle type.
    unsigned gullPasses=0,planePasses=0,duckFrames=0;
    std::array<bool,SupplyRun::Obstacles.size()> seen{};
    int firstGap=-1;bool variedGap=false;
    for(unsigned frame=0;frame<3600&&!game.crashed();++frame){
        const auto before=game.obstacles();
        const SupplyRun::Obstacle* next=nullptr;
        for(const auto& obstacle:game.obstacles()){
            assert(obstacle.kind<seen.size());seen[obstacle.kind]=true;
            if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
            if(!next||obstacle.x<next->x)next=&obstacle;
        }
        const bool duck=next&&SupplyRun::overhead(next->kind)&&next->x<265;
        game.setDucking(duck);
        if(duck)++duckFrames;
        if(next&&!SupplyRun::overhead(next->kind)&&next->x>SupplyRun::PlayerX&&next->x<240&&game.height()==0)game.queueJump();
        for(const auto& obstacle:game.obstacles()){
            if(obstacle.x<125&&obstacle.x>113){
                if(obstacle.kind==3)++gullPasses;
                if(obstacle.kind==7)++planePasses;
            }
        }
        game.update(now+=33);
        for(std::size_t i=0;i<before.size();++i){
            const auto& current=game.obstacles()[i];
            if(current.x<=before[i].x+100)continue;
            float last=-1000;
            for(std::size_t j=0;j<before.size();++j)if(j!=i)last=std::max(last,game.obstacles()[j].x);
            const float gap=current.x-last;
            assert(gap>=329.9f&&gap<=529.1f);
            if(firstGap<0)firstGap=int(gap);
            else if(int(gap)!=firstGap)variedGap=true;
        }
    }
    assert(!game.crashed());
    assert(game.score()>2000&&gullPasses>0&&planePasses>0&&duckFrames>0);
    assert(game.best()==game.score());
    assert(variedGap&&std::all_of(seen.begin(),seen.end(),[](bool value){return value;}));

    // Independent seeds exercise varied flight heights and jump/duck transitions.
    for(unsigned seed:{1u,7u,59u,1000u,123456u,0xfffffff0u}){
        SupplyRun varied;unsigned tick=seed;varied.resume(tick);
        for(unsigned frame=0;frame<3600&&!varied.crashed();++frame){
            const SupplyRun::Obstacle* next=nullptr;
            for(const auto& obstacle:varied.obstacles()){
                if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
                if(!next||obstacle.x<next->x)next=&obstacle;
            }
            varied.setDucking(next&&SupplyRun::overhead(next->kind)&&next->x<265);
            if(next&&!SupplyRun::overhead(next->kind)&&next->x>SupplyRun::PlayerX&&next->x<240&&varied.height()==0)varied.queueJump();
            varied.update(tick+=33);
        }
        assert(!varied.crashed()&&varied.score()>2000);
    }

    // Ignoring duck still collides with the first overhead seagull.
    SupplyRun noDuck;
    for(unsigned tick=33;tick<6000&&!noDuck.crashed();tick+=33){
        if(noDuck.powerupVisible()&&noDuck.powerupX()>SupplyRun::PlayerX+30&&noDuck.powerupX()<240&&noDuck.height()==0)noDuck.queueJump();
        for(const auto& obstacle:noDuck.obstacles())
            if(!SupplyRun::overhead(obstacle.kind)&&obstacle.x>SupplyRun::PlayerX&&obstacle.x<240&&noDuck.height()==0)noDuck.queueJump();
        noDuck.update(tick);
    }
    assert(noDuck.crashed());

    // Collecting Lisa gives a bounded shield and a score bonus; the game still
    // advances, and ordinary collisions resume when the shield runs out.
    const auto pilot=[](SupplyRun& run){
        const SupplyRun::Obstacle* next=nullptr;
        for(const auto& obstacle:run.obstacles()){
            if(obstacle.x+SupplyRun::Obstacles[obstacle.kind].width<SupplyRun::PlayerX)continue;
            if(!next||obstacle.x<next->x)next=&obstacle;
        }
        run.setDucking(next&&SupplyRun::overhead(next->kind)&&next->x<265);
        if(next&&!SupplyRun::overhead(next->kind)&&next->x>SupplyRun::PlayerX&&next->x<240&&run.height()==0)run.queueJump();
    };
    SupplyRun shield;unsigned shieldTick=0;
    for(unsigned frame=0;frame<1800&&!shield.pickups();++frame){pilot(shield);shield.update(shieldTick+=33);assert(!shield.crashed());}
    assert(shield.pickups()==1&&shield.invincible()&&!shield.powerupVisible());
    assert(shield.score()>=unsigned(shield.distance()/10)+100);
    bool crossedHazard=false;
    shield.setDucking(false);
    for(unsigned frame=0;frame<70;++frame){
        shield.update(shieldTick+=33);
        for(const auto& obstacle:shield.obstacles())if(obstacle.x<SupplyRun::PlayerX+SupplyRun::PlayerWidth&&obstacle.x+SupplyRun::Obstacles[obstacle.kind].width>SupplyRun::PlayerX)crossedHazard=true;
        assert(!shield.crashed()&&shield.invincible());
    }
    assert(crossedHazard);
    const auto beforePause=shield.powerupSeconds();shieldTick+=50000;shield.resume(shieldTick);shield.update(shieldTick+=33);
    assert(shield.powerupSeconds()>beforePause-.04f);
    for(unsigned frame=0;frame<160&&shield.invincible();++frame){pilot(shield);shield.update(shieldTick+=33);assert(!shield.crashed());}
    assert(!shield.invincible());
    for(unsigned frame=0;frame<300&&!shield.crashed();++frame){shield.setDucking(false);shield.update(shieldTick+=33);}
    assert(shield.crashed());shieldTick+=1000;shield.queueJump();shield.update(shieldTick+=33);
    assert(!shield.crashed()&&!shield.invincible()&&shield.pickups()==0&&shield.score()==0);

    SupplyRun wrap;
    wrap.resume(0xfffffff0u);wrap.queueJump();wrap.update(20);
    assert(wrap.height()>0&&wrap.distance()<15);
    std::cout<<"Supply Run: all eleven obstacles, jump/duck, safe randomized gaps, Lisa pickup/bonus/shield/expiry/restart, bounded timing and clock wrap passed\n";
}
