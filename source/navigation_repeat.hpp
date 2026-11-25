#pragma once

#include "input.hpp"
#include <array>
#include <cstdint>

namespace unbound {

// Repeat only navigation, never destructive/select buttons. The UI owns this
// state; no controller polling or filesystem work occurs on worker threads.
class NavigationRepeat {
public:
    std::uint32_t update(std::uint32_t pressed,std::uint32_t held,std::uint32_t now) {
        constexpr std::array<std::uint32_t,4> directions{
            button::Up,button::Down,button::Left,button::Right};
        held|=pressed;
        if((held&(button::Up|button::Down))==(button::Up|button::Down))held&=~(button::Up|button::Down);
        if((held&(button::Left|button::Right))==(button::Left|button::Right))held&=~(button::Left|button::Right);
        std::uint32_t result=0;
        for(std::size_t i=0;i<directions.size();++i){
            const auto bit=directions[i];
            if(!(held&bit)){active_[i]=false;continue;}
            if(!active_[i]||(pressed&bit)){
                active_[i]=true;next_[i]=now+350;result|=bit;
            }else if(static_cast<std::int32_t>(now-next_[i])>=0){
                next_[i]=now+100;result|=bit;
            }
        }
        return result;
    }
private:
    std::array<bool,4> active_{};
    std::array<std::uint32_t,4> next_{};
};

} // namespace unbound
