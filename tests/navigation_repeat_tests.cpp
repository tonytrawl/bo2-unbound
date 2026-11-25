#include "navigation_repeat.hpp"
#include <cassert>
#include <iostream>

int main(){
    using namespace unbound;
    NavigationRepeat repeat;
    assert(repeat.update(button::Down,button::Down,0)==button::Down);
    assert(repeat.update(0,button::Down,349)==0);
    assert(repeat.update(0,button::Down,350)==button::Down);
    assert(repeat.update(0,button::Down,449)==0);
    assert(repeat.update(0,button::Down,450)==button::Down);
    assert(repeat.update(0,0,451)==0);
    assert(repeat.update(0,button::Down,452)==button::Down);
    assert(repeat.update(button::A|button::X,button::A|button::X,453)==0);
    assert(repeat.update(button::Up|button::Down,button::Up|button::Down,454)==0);
    assert(repeat.update(button::Left|button::Right,button::Left|button::Right,455)==0);
    NavigationRepeat rollover;
    assert(rollover.update(button::Right,button::Right,0xffffff00u)==button::Right);
    assert(rollover.update(0,button::Right,92u)==0);
    assert(rollover.update(0,button::Right,94u)==button::Right);
    assert(rollover.update(0,button::Right,194u)==button::Right);
    std::cout<<"Navigation repeat delay, held input, opposing keys and rollover tests passed\n";
}
