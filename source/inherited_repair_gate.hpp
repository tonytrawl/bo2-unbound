#pragma once
#include <string>
#include <utility>

namespace unbound {
// Current update.txt never clears an incomplete inherited-content repair.
class InheritedRepairGate {
public:
    void clear(){target_.clear();blocked_=false;detail_.clear();}
    void observe(std::string target,bool inspected,bool needed,std::string detail){
        target_=std::move(target);blocked_=!inspected||needed;detail_=std::move(detail);
    }
    bool blocked()const{return blocked_;}
    const std::string& detail()const{return detail_;}
    const std::string& target()const{return target_;}
private:
    std::string target_,detail_;
    bool blocked_=false;
};
}
