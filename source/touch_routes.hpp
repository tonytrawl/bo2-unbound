#pragma once

#include "input.hpp"
#include "touch.hpp"

namespace unbound {

// Touch sends exactly one controller-equivalent action, never a chord.
inline std::uint32_t touchButtonMask(const TouchAction& action) {
    if(action.kind!=TouchActionKind::Button)return 0;
    switch(static_cast<std::uint32_t>(action.value)){
    case button::A:case button::B:case button::X:case button::Y:
    case button::L:case button::Plus:case button::Minus:
    case button::Left:case button::Right:case button::Up:case button::Down:
        return static_cast<std::uint32_t>(action.value);
    default:return 0;
    }
}

inline std::uint32_t dismissBeforeActivate(std::uint32_t buttons) {
    if(buttons&(button::B|button::Plus))
        buttons&=~(button::A|button::X|button::Y|button::Minus);
    return buttons;
}

} // namespace unbound
