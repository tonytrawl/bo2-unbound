#pragma once
#include "model.hpp"
#include <array>
#include <string>
#include <cstdint>
#include <SDL2/SDL.h>
namespace unbound {
struct NetworkStats {
    bool available=false, stale=false, heatAvailable=false, heatStale=false;
    unsigned players=0, servers=0, unique=0;
    std::array<unsigned,5> platforms{};
    std::array<unsigned char,648> cells{};
    std::uint32_t received=0, heatReceived=0;
};
bool parseNetworkStats(const std::string&, NetworkStats&);
bool parseGhostsNetworkStats(const std::string&, NetworkStats&);
bool parseNetworkHeatmap(const std::string&, NetworkStats&);
class NetworkFeed {
public:
    ~NetworkFeed(){stop();}
    bool tick(bool visible, std::uint32_t now);
    void stop();
    void setGame(Game game);
    void pause(){SDL_AtomicSet(&cancel_,1);}
    const NetworkStats& snapshot() const{return current_;}
private:
    static int run(void*);
    SDL_Thread* thread_=nullptr;
    SDL_atomic_t done_{}, cancel_{};
    NetworkStats current_, result_;
    std::uint32_t nextStats_=0,nextHeat_=0;
    bool fetchHeat_=false, started_=false;
    Game game_=Game::BlackOpsII;
};
}
