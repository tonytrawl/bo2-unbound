#include "touch_scroll.hpp"
#include <cassert>
#include <iostream>
using namespace unbound;
int main(){
    TouchScroll g;int y=100;
    assert(!g.update({true,true,400,400},0,1000,y)); // carried touch blocked
    g.update({true,false,0,0},16,1000,y);
    g.update({true,true,400,400},32,1000,y);
    assert(!g.update({true,true,400,397},48,1000,y)&&y==100);
    assert(g.update({true,true,400,350},64,1000,y)&&y>100);
    const int before=y;g.update({true,false,0,0},80,1000,y);assert(y>before);
    for(unsigned t=96;t<4000;t+=16)g.update({true,false,0,0},t,1000,y);
    int stopped=y;g.update({true,false,0,0},4016,1000,y);assert(y==stopped&&y<=1000);
    g.reset();g.update({true,false,0,0},5000,1000,y);
    g.update({true,true,20,200},5016,1000,y);g.update({true,true,400,100},5032,1000,y);assert(y==stopped);
    g.reset();g.update({true,false,0,0},6000,0,y);y=0;
    g.update({true,true,400,400},6016,0,y);g.update({true,true,400,100},6032,0,y);assert(y==0);
    g.update({},6048,1000,y);assert(!g.update({true,true,400,200},6064,1000,y));
    g.reset();g.update({true,false,0,0},7000,1000,y);
    g.update({true,true,400,400},7016,1000,y);g.update({true,true,400,300},7032,1000,y);
    g.update({true,true,400,300},7200,1000,y);stopped=y;
    g.update({true,false,0,0},7216,1000,y);assert(y==stopped); // hold stops fling
    g.reset();g.update({true,false,0,0},0xfffffff0u,1000,y);
    g.update({true,true,400,400},0xfffffff8u,1000,y);
    g.update({true,true,400,300},8,1000,y);assert(y<=1000);
    g.reset();y=0;
    const TouchRect queue{244,212,606,342};
    g.update({true,false,0,0},8000,500,y,queue);
    g.update({true,true,1000,300},8016,500,y,queue);
    g.update({true,true,1000,200},8032,500,y,queue);assert(y==0);
    g.update({true,false,0,0},8048,500,y,queue);
    g.update({true,true,400,400},8064,500,y,queue);
    assert(g.update({true,true,400,300},8080,500,y,queue)&&y>0);
    std::cout<<"Touch scrolling: drag, inertia, hold, bounds, transitions, invalid samples and clock wrap passed\n";
}
