#include "network_stats.hpp"
#include "network_json.hpp"
namespace unbound {
bool parseNetworkStats(const std::string& text,NetworkStats& out){
 network_json::Value v; if(!network_json::Parser(text).parse(v)||v.type!='{')return false;
 NetworkStats next=out;
 if(!v.get("players").count(next.players)||!v.get("servers").count(next.servers)||!v.get("unique_today").count(next.unique))return false;
 next.platforms={};const char* keys[]={"pc","wiiu","switch","android"};
 for(int i=0;i<4;++i)v.get("players_by_platform").get(keys[i]).count(next.platforms[i]);
 if(!next.platforms[2])v.get("players_by_platform").get("nx").count(next.platforms[2]);
 unsigned known=0;for(int i=0;i<4;++i)known+=next.platforms[i];
 next.platforms[4]=next.players>known?next.players-known:0;
 next.available=true;next.stale=false;out=next;return true;
}
bool parseGhostsNetworkStats(const std::string& text, NetworkStats& out) {
 network_json::Value value;
 if(!network_json::Parser(text).parse(value)||value.type!='{')return false;
 const auto& available=value.get("available");
 NetworkStats next=out;
 if(available.type!='b'||!available.boolean||
    !value.get("host_reported_players").count(next.players)||
    !value.get("advertised_lobbies").count(next.servers))return false;
 next.unique=0;
 next.platforms={};
 next.available=true;
 next.stale=false;
 out=next;
 return true;
}
bool parseNetworkHeatmap(const std::string& text,NetworkStats& out){
 network_json::Value v;unsigned n=0;
 if(!network_json::Parser(text).parse(v)||v.type!='{'||!v.get("version").count(n)||n!=1||!v.get("width").count(n)||n!=36||!v.get("height").count(n)||n!=18||v.get("precision").string!="country"||v.get("available").type!='b')return false;
 const auto& stale=v.get("stale");
 if(!v.get("available").boolean){out.heatStale=false;out.heatAvailable=false;out.cells={};return true;}
 const auto& cells=v.get("cells");if(cells.type!='['||cells.array.size()!=648)return false;
 std::array<unsigned char,648> parsed{};for(size_t i=0;i<648;++i){if(!cells.array[i].count(n)||n>255)return false;parsed[i]=n;}
 out.cells=parsed;out.heatAvailable=true;out.heatStale=stale.type=='b'&&stale.boolean;return true;
}
}
