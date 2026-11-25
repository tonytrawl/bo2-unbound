#include "install_activity.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {
bool contains(const std::string& source,const std::string& expected) {
    return source.find(expected)!=std::string::npos;
}
void firstObservationAndPulse() {
    unbound::InstallActivity activity(1000);
    assert(!activity.pulseDue(1000)&&!activity.pulseDue(100000));
    const auto empty=activity.snapshot(100000);
    assert(empty.title.empty()&&empty.detail.empty()&&empty.percent==-1);
    activity.observe("Checking package","Reading ZIP directory...",-1,2000);
    assert(!activity.pulseDue(2999)&&activity.pulseDue(3000));
    const auto first=activity.snapshot(3000);
    assert(first.title=="Checking package"&&first.percent==-1);
    assert(first.detail=="Reading ZIP directory...\nElapsed 00:02");
    assert(!activity.pulseDue(3000)&&!activity.pulseDue(3999)&&activity.pulseDue(4000));
    const auto next=activity.snapshot(4000);
    assert(contains(next.detail,"Elapsed 00:03"));
    assert(!contains(next.detail,"No new progress"));
}
void stageAndDetailChanges() {
    unbound::InstallActivity activity(0);
    const std::string detail="256 MB of 1 GB\nTime remaining: 1m 20s | 12 MiB/s";
    activity.observe("Installing files",detail,25,2000);
    auto view=activity.snapshot(5000);
    assert(view.percent==25&&view.detail.rfind(detail,0)==0);
    assert(contains(view.detail,"Elapsed 00:05"));
    activity.observe("Installing files","300 MB of 1 GB",30,7000);
    view=activity.snapshot(10000);
    assert(view.percent==30&&view.detail.rfind("300 MB of 1 GB\n",0)==0);
    assert(contains(view.detail,"Elapsed 00:10"));
    // A detail/percentage change is genuine activity, but not a new stage.
    assert(!contains(view.detail,"No new progress"));
    activity.observe("Saving installed version","Closing package...",99,12000);
    view=activity.snapshot(13000);
    assert(view.title=="Saving installed version"&&view.percent==99);
    assert(contains(view.detail,"Elapsed 00:13"));
    activity.observe("Saving installed version","Closing package...",100,14000);
    view=activity.snapshot(15000);
    assert(view.percent==100&&contains(view.detail,"Elapsed 00:15"));
}
void unchangedStatusAndSilence() {
    unbound::InstallActivity activity(0);
    activity.observe("Checking destination","Waiting on storage...",-1,0);
    for(const std::uint32_t now:{1000,5000,10000,14999}){
        activity.observe("Checking destination","Waiting on storage...",-1,now);
        assert(!activity.pulseDue(now)); // The real callback already rendered.
    }
    auto view=activity.snapshot(14999);
    assert(!contains(view.detail,"No new progress"));
    view=activity.snapshot(15000);
    assert(view.percent==-1);
    assert(contains(view.detail,"This step is taking longer. Keep the console on."));
    assert(contains(view.detail,"Elapsed 00:15"));
    assert(!activity.pulseDue(15999)&&activity.pulseDue(16000));
    view=activity.snapshot(61000);
    assert(contains(view.detail,"Elapsed 01:01"));
    assert(contains(view.detail,"This step is taking longer."));
    // Progress changes clear the silence notice without resetting stage time.
    activity.observe("Checking destination","Next directory...",-1,62000);
    view=activity.snapshot(63000);
    assert(!contains(view.detail,"taking longer"));
    assert(contains(view.detail,"Elapsed 01:03"));
    activity.observe("Checking destination","Next directory...",0,64000);
    view=activity.snapshot(65000);
    assert(view.percent==0&&!contains(view.detail,"No new progress"));
    activity.observe("Checking space","",-1,66000);
    view=activity.snapshot(67000);
    assert(view.detail=="Elapsed 01:07");
}
void tickWrapAndExactPercentage() {
    constexpr auto start=std::numeric_limits<std::uint32_t>::max()-499;
    unbound::InstallActivity activity(start);
    activity.observe("Opening ZIP","Checking archive",-1,start);
    assert(!activity.pulseDue(499)&&activity.pulseDue(500));
    auto view=activity.snapshot(500);
    assert(view.detail=="Checking archive\nElapsed 00:01");
    view=activity.snapshot(14500);
    assert(contains(view.detail,"taking longer"));
    activity.observe("Opening ZIP","Checking archive",-1,15000);
    view=activity.snapshot(15500);
    assert(contains(view.detail,"Elapsed 00:16"));
    assert(contains(view.detail,"taking longer"));
    activity.observe("Installing","3 bytes",37,16000);
    view=activity.snapshot(17000);
    assert(view.percent==37&&contains(view.detail,"Elapsed 00:17"));
    activity.observe("Installing","3 bytes",-1,18000);
    view=activity.snapshot(19000);
    assert(view.percent==-1); // No clamping or manufactured progress.
}
}

int main() {
    firstObservationAndPulse();stageAndDetailChanges();unchangedStatusAndSilence();tickWrapAndExactPercentage();
    std::cout<<"Install activity elapsed time, stage changes, silence, pulse and tick-wrap tests passed\n";
}
