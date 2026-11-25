#pragma once

#include <cstdint>
#include <string>

namespace unbound {

struct InstallActivitySnapshot {
    std::string title;
    std::string detail;
    int percent=-1;
};

// Main-thread presentation state only. Real progress remains authoritative:
// this adds elapsed time and a brief wait notice, never bytes, an ETA, a percentage,
// storage work, cancellation or any change to the worker's lifetime.
class InstallActivity {
public:
    explicit InstallActivity(std::uint32_t started):started_(started) {}

    void observe(const std::string& title,const std::string& detail,int percent,
                  std::uint32_t now) {
        const bool changed=!observed_||title!=latest_.title||detail!=latest_.detail||percent!=latest_.percent;
        if(changed)lastChanged_=now;
        latest_={title,detail,percent};
        observed_=true;
        lastRendered_=now;
    }

    bool pulseDue(std::uint32_t now) const {
        return observed_&&now-lastRendered_>=1000;
    }

    // Consumes the display pulse. Call this for real progress renders too so
    // the original detail and the elapsed information use the same snapshot.
    InstallActivitySnapshot snapshot(std::uint32_t now) {
        if(!observed_)return {};
        lastRendered_=now;
        auto result=latest_;
        if(!result.detail.empty())result.detail+='\n';
        result.detail+="Elapsed "+clock(now-started_);
        const auto quiet=now-lastChanged_;
        if(quiet>=15000)
            result.detail+="\nThis step is taking longer. Keep the console on.";
        return result;
    }

private:
    static std::string clock(std::uint32_t milliseconds) {
        const auto seconds=milliseconds/1000;
        const auto minutes=seconds/60;
        const auto remainder=seconds%60;
        return (minutes<10?"0":"")+std::to_string(minutes)+":"+
               (remainder<10?"0":"")+std::to_string(remainder);
    }

    InstallActivitySnapshot latest_;
    std::uint32_t started_=0;
    std::uint32_t lastChanged_=0;
    std::uint32_t lastRendered_=0;
    bool observed_=false;
};

} // namespace unbound
