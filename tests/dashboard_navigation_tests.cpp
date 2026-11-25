#include "dashboard_navigation.hpp"
#include <cassert>
int main(){using namespace unbound;DashboardNavigation n;
assert(n.action()==4&&!n.menuFocused());n.move(button::Up);assert(n.action()==5);n.move(button::Up);assert(n.action()==0);
n.move(button::Down);assert(n.action()==5);n.move(button::Down);assert(n.action()==1);n.move(button::Right);assert(n.action()==4);n.move(button::Left);assert(n.action()==1);
n.move(button::B);assert(n.menuFocused());for(int i=0;i<5;++i)n.move(button::Down);assert(n.section()==5);assert(!n.move(button::Down));n.returnToMenu(99);assert(n.section()==0);
n.selectAction(5);assert(n.action()==5&&!n.menuFocused());
n.move(button::Up);assert(n.action()==0);
assert(n.move(button::Up,1)&&n.gameChoice()==1);
assert(n.move(button::Left)&&n.gameChoice()==0);
assert(n.move(button::B)&&n.gameChoice()==-1&&n.action()==0);
n.returnToMenu(0);
assert(n.move(button::Up,1)&&n.gameChoice()==1);
assert(n.move(button::Down)&&n.gameChoice()==-1&&n.menuFocused());
}
