#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace unbound {

// UI-thread-only state. The game has no files, networking, worker threads or
// callbacks into the package operation. A slow frame advances at most 100 ms.
class SupplyRun {
public:
    struct Obstacle { float x; unsigned kind; float lift=-1; };
    struct ObstacleDefinition {
        std::string_view joke;
        int spriteHeight;
        float width, height, lift;
    };
    static constexpr std::array<ObstacleDefinition,11> Obstacles{{
        {"Budget Pack-a-Punch",20,38,36,0},
        {"Nuketown roadworks",21,38,40,0},
        {"Zombies ordered extra cheese",18,42,34,0},
        {"seagull delayed the update again",18,48,32,44},
        {"GamePad battery: 1%",18,54,32,0},
        {"Mystery Box says bye-bye!",26,44,48,0},
        {"Juggernog: still 10 points short",36,40,66,0},
        {"Mob Airlines: no return flight",18,56,30,46},
        {"Brutus skipped leg day",36,40,66,0},
        {"Buried witch: hide your points",36,34,64,0},
        {"Panzer wants your download speed",36,44,68,0}
    }};
    static constexpr float Width=1152, PlayerX=136, PlayerWidth=34, PlayerHeight=68, DuckHeight=32;
    static constexpr float PowerupDuration=6, PowerupWidth=40, PowerupHeight=56;

    SupplyRun(){restart(0);}
    void resume(std::uint32_t now){
        lastTick_=now;
        if(!seeded_){random_^=now+0x9e3779b9u;if(!random_)random_=0xB0217;seeded_=true;}
    }
    void queueJump(){jumpQueued_=true;}
    void setDucking(bool held){duckHeld_=held;}
    bool ducking()const{return duckHeld_&&height_==0;}
    static bool overhead(unsigned kind){return Obstacles[kind].lift>0;}
    static float obstacleLift(const Obstacle& obstacle){
        return obstacle.lift>=0?obstacle.lift:Obstacles[obstacle.kind].lift;
    }
    void update(std::uint32_t now){
        const auto elapsed=std::min<std::uint32_t>(now-lastTick_,100);
        lastTick_=now;
        if(crashed_){
            if(jumpQueued_&&now-crashedAt_>=300)restart(now);
            jumpQueued_=false;
            return;
        }
        if(jumpQueued_&&height_==0&&!duckHeld_)velocity_=530;
        jumpQueued_=false;
        float remaining=elapsed/1000.0f;
        while(remaining>0){
            const float dt=std::min(remaining,1.0f/120);
            remaining-=dt;
            height_=std::max(0.0f,height_+velocity_*dt);
            velocity_=height_>0?velocity_-1400*dt:0;
            const float speed=std::min(360.0f,235+distance_/220);
            distance_+=speed*dt;
            shieldSeconds_=std::max(0.0f,shieldSeconds_-dt);
            if(powerupVisible_){
                powerupX_-=speed*dt;
                if(PlayerX+PlayerWidth>powerupX_+6&&PlayerX<powerupX_+PowerupWidth-6&&height_+8<PowerupHeight){
                    shieldSeconds_=PowerupDuration;bonus_+=100;++pickups_;
                    powerupVisible_=false;powerupGap_=2100+float(nextRandom()%900);
                }else if(powerupX_<-80){powerupX_=Width+400+float(nextRandom()%600);}
            }else{
                powerupGap_-=speed*dt;
                if(powerupGap_<=0){powerupVisible_=true;powerupX_=Width+64;}
            }
            for(auto& obstacle:obstacles_)obstacle.x-=speed*dt;
            for(auto& obstacle:obstacles_){
                if(obstacle.x<-80){
                    const float last=std::max({obstacles_[0].x,obstacles_[1].x,obstacles_[2].x});
                    const float gap=330+float(nextRandom()%200);
                    unsigned kind=nextRandom()%Obstacles.size();
                    if(kind==lastSpawnKind_)kind=(kind+1)%Obstacles.size();
                    lastSpawnKind_=kind;
                    const float lift=Obstacles[kind].lift+(overhead(kind)?float(nextRandom()%3)*6:0);
                    obstacle={last+gap,kind,lift};
                }
                const auto& definition=Obstacles[obstacle.kind];
                const float lift=obstacleLift(obstacle);
                const float bodyHeight=ducking()?DuckHeight:PlayerHeight;
                if(!invincible()&&PlayerX+PlayerWidth>obstacle.x+6&&PlayerX<obstacle.x+definition.width-6&&
                   height_+8<lift+definition.height&&height_+bodyHeight>lift+6){
                    crashed_=true;crashedAt_=now;
                    best_=std::max(best_,score());
                    return;
                }
            }
        }
        best_=std::max(best_,score());
    }
    unsigned score()const{return static_cast<unsigned>(distance_/10)+bonus_;}
    unsigned best()const{return best_;}
    float height()const{return height_;}
    float distance()const{return distance_;}
    bool crashed()const{return crashed_;}
    bool invincible()const{return shieldSeconds_>0;}
    float powerupSeconds()const{return shieldSeconds_;}
    float powerupX()const{return powerupX_;}
    bool powerupVisible()const{return powerupVisible_;}
    unsigned pickups()const{return pickups_;}
    const std::array<Obstacle,3>& obstacles()const{return obstacles_;}

private:
    std::uint32_t nextRandom(){
        random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
        return random_;
    }
    void restart(std::uint32_t now){
        distance_=height_=velocity_=0;
        shieldSeconds_=0;bonus_=pickups_=0;powerupX_=920;powerupGap_=0;powerupVisible_=true;
        obstacles_={{{390,0},{740,2},{1080,3}}};
        crashed_=false;lastTick_=now;duckHeld_=false;lastSpawnKind_=3;
    }
    std::array<Obstacle,3> obstacles_{};
    float height_=0,velocity_=0,distance_=0;
    float shieldSeconds_=0,powerupX_=920,powerupGap_=0;
    unsigned best_=0,lastSpawnKind_=3,bonus_=0,pickups_=0;
    std::uint32_t lastTick_=0,crashedAt_=0,random_=0xB0217;
    bool crashed_=false,jumpQueued_=false,seeded_=false,duckHeld_=false;
    bool powerupVisible_=true;
};
}
