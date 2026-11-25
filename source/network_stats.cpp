#include "network_stats.hpp"
#include "https_curl.hpp"
#include <string_view>
namespace unbound {
namespace {
struct Request {std::string text;SDL_atomic_t* cancel;};
size_t append(void* data,size_t size,size_t count,void* opaque){auto& r=*static_cast<Request*>(opaque);if(size&&count>65536/size)return 0;const auto bytes=size*count;if(r.text.size()+bytes>65536)return 0;r.text.append(static_cast<char*>(data),bytes);return bytes;}
int progress(void* p,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return SDL_AtomicGet(static_cast<SDL_atomic_t*>(p));}
bool fetch(const char* url,std::string& output,SDL_atomic_t& cancel){
 CURL* curl=curl_easy_init();if(!curl)return false;Request request{{},&cancel};
 std::string error;
 if(std::string_view(url).starts_with("https://")&&!configureHttpsTrust(curl,error)){curl_easy_cleanup(curl);return false;}
 curl_easy_setopt(curl,CURLOPT_URL,url);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(curl,CURLOPT_IPRESOLVE,CURL_IPRESOLVE_V4);
 curl_easy_setopt(curl,CURLOPT_USERAGENT,"BO2-Unbound-HQ Network/1");
 curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,3L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,7L);
 curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&request);
 curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&cancel);
 const auto result=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(curl);
 if(result!=CURLE_OK||status!=200||SDL_AtomicGet(&cancel))return false;
 output=std::move(request.text);return true;
}
}
int NetworkFeed::run(void* opaque){auto& self=*static_cast<NetworkFeed*>(opaque);std::string text;
 const bool ghosts=self.game_==Game::Ghosts;
 const char* statsUrl=ghosts?"https://unc-bo2.fun/api/ghosts/stats":"http://connect.unc-bo2.fun:20801/stats.json";
 const char* heatUrl=ghosts?"https://unc-bo2.fun/api/ghosts/heatmap":"http://connect.unc-bo2.fun:20801/heatmap.json";
 const auto parseStats=ghosts?parseGhostsNetworkStats:parseNetworkStats;
 if(fetch(statsUrl,text,self.cancel_)&&parseStats(text,self.result_))self.result_.received=SDL_GetTicks();else self.result_.stale=true;
 if(self.fetchHeat_&&!SDL_AtomicGet(&self.cancel_)){if(fetch(heatUrl,text,self.cancel_)&&parseNetworkHeatmap(text,self.result_)){self.result_.heatReceived=SDL_GetTicks();}else self.result_.heatStale=true;}
 SDL_AtomicSet(&self.done_,1);return 0;
}
bool NetworkFeed::tick(bool visible,std::uint32_t now){bool changed=false;
 if(thread_&&SDL_AtomicGet(&done_)){SDL_WaitThread(thread_,nullptr);thread_=nullptr;if(!SDL_AtomicGet(&cancel_)){current_=result_;changed=true;if(fetchHeat_&&current_.heatReceived==0)nextHeat_=now+60000;}else started_=false;}
 if(!visible){SDL_AtomicSet(&cancel_,1);return changed;}
 if(!thread_&&(!started_||static_cast<std::int32_t>(now-nextStats_)>=0)){
  fetchHeat_=!started_||static_cast<std::int32_t>(now-nextHeat_)>=0;started_=true;nextStats_=now+60000;if(fetchHeat_)nextHeat_=now+900000;
  result_=current_;SDL_AtomicSet(&cancel_,0);SDL_AtomicSet(&done_,0);
  thread_=SDL_CreateThreadWithStackSize(run,"HQ network stats",256*1024,this);
  if(!thread_){current_.stale=true;changed=true;}
 }return changed;
}
void NetworkFeed::setGame(Game game){
 if(game_==game)return;
 stop();
 game_=game;
 current_={};
 result_={};
 started_=false;
}
void NetworkFeed::stop(){SDL_AtomicSet(&cancel_,1);if(thread_){SDL_WaitThread(thread_,nullptr);thread_=nullptr;}}
}
