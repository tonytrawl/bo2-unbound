#include "touch_routes.hpp"
#include "dashboard_navigation.hpp"
#include <cassert>
#include <cstdio>

int main(){
    using namespace unbound;
    for(const auto bit:{button::A,button::B,button::X,button::Y,button::L,
                       button::Plus,button::Minus,button::Left,button::Right,
                       button::Up,button::Down})
        assert(touchButtonMask({TouchActionKind::Button,static_cast<int>(bit)})==bit);
    assert(touchButtonMask({TouchActionKind::Button,static_cast<int>(button::A|button::B)})==0);
    assert(touchButtonMask({TouchActionKind::Sidebar,static_cast<int>(button::A)})==0);
    assert(touchButtonMask({TouchActionKind::Button,-1})==0);
    assert(dismissBeforeActivate(button::A|button::B)==button::B);
    assert(dismissBeforeActivate(button::A|button::Plus|button::X|button::Y|button::Minus)==button::Plus);
    assert(dismissBeforeActivate(button::A|button::Down)==(button::A|button::Down));
    DashboardNavigation navigation;
    navigation.returnToMenu(4);
    navigation.selectAction(0);
    assert(!navigation.menuFocused()&&navigation.action()==0&&navigation.section()==0);
    navigation.selectAction(1);assert(navigation.action()==1);
    navigation.selectAction(4);assert(navigation.action()==4);
    navigation.selectAction(3);assert(navigation.action()==4);
    navigation.returnToMenu(4);
    assert(navigation.menuFocused()&&navigation.section()==4&&navigation.action()==4);
    for(int section=0;section<=5;++section){
        navigation.selectAction(1);
        navigation.returnToMenu(section);
        assert(navigation.move(button::Right));
        assert(!navigation.menuFocused()&&navigation.section()==0&&navigation.action()==1);
        navigation.returnToMenu(section);
        navigation.move(button::A);
        assert(navigation.menuFocused()&&navigation.section()==section);
    }
    std::puts("Touch action mapping, controller dismissal priority and dashboard selection tests passed");
}
