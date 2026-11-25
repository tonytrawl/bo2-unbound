#include "network_stats.hpp"
#include "globe_motion.hpp"
#include <cassert>
#include <cmath>
#include <string>
#include <fstream>
#include <iterator>
int main(){using namespace unbound;NetworkStats s;
assert(parseNetworkStats(R"({"players":12,"servers":3,"unique_today":40,"players_by_platform":{"pc":7,"wiiu":5}})",s));assert(s.players==12&&s.platforms[1]==5);
assert(!parseNetworkStats(R"({"players":-1,"servers":3,"unique_today":40})",s));assert(s.players==12);
assert(!parseNetworkStats(R"({"players":12,"players":13,"servers":3,"unique_today":40})",s));
assert(!parseNetworkStats(R"({"players":12.5,"servers":3,"unique_today":40})",s));
assert(!parseNetworkStats(std::string(65537,' '),s));
std::string h=R"({"version":1,"width":36,"height":18,"precision":"country","available":true,"cells":[)";
for(int i=0;i<648;++i){if(i)h+=",";h+=i==300?"255":"0";}h+="]}";
assert(parseNetworkHeatmap(h,s)&&s.heatAvailable&&s.cells[300]==255);
auto invalid=h;invalid.replace(invalid.find("255"),3,"256");assert(!parseNetworkHeatmap(invalid,s));assert(s.cells[300]==255);
assert(parseNetworkHeatmap(R"({"version":1,"width":36,"height":18,"precision":"country","available":false})",s)&&!s.heatAvailable);
for(const char* file:{"tests/network-live-stats.json","tests/network-live-heatmap.json"}){
 std::ifstream in(file);assert(in);std::string body((std::istreambuf_iterator<char>(in)),{});
 assert(std::string(file).find("heatmap")!=std::string::npos?parseNetworkHeatmap(body,s):parseNetworkStats(body,s));
}
assert(parseGhostsNetworkStats(R"({"available":true,"generated":10,"host_reported_players":17,"advertised_lobbies":3})",s));
assert(s.players==17&&s.servers==3&&s.unique==0&&s.platforms[1]==0);
assert(!parseGhostsNetworkStats(R"({"available":false,"host_reported_players":0,"advertised_lobbies":0})",s));
assert(s.players==17);
assert(parseGhostsNetworkStats(R"({"available":true,"host_reported_players":0,"advertised_lobbies":0})",s));
assert(s.available&&s.players==0&&s.servers==0);
assert(parseNetworkHeatmap(h.substr(0,h.size()-1)+R"(,"stale":true})",s)&&s.heatStale);
GlobeMotion g;g.update({true,true,554,477},100,true);float initial=g.yaw;g.update({true,true,650,477},116,true);assert(g.yaw>=initial); // entry-held finger cannot drag
 g.update({true,false,0,0},132,true);g.update({true,true,554,477},148,true);float start=g.yaw;
 assert(g.update({true,true,594,497},164,true));assert(g.yaw<start&&g.pitch>0);
 float release=g.yaw;g.update({true,false,594,497},180,true);assert(g.yaw<release);
 for(unsigned t=196;t<6000;t+=16)g.update({true,false,0,0},t,true);
 float settled=g.yaw;g.update({true,false,0,0},6016,true);assert(g.yaw>settled);assert(std::abs(g.pitch)<=1.1f);
 g.resetGesture();float before=g.yaw;g.update({true,true,10,10},6032,true);g.update({true,true,100,10},6048,true);assert(std::abs(g.yaw-before)<.01f);
}
