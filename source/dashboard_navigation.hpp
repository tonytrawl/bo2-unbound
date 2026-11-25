#pragma once

#include "input.hpp"
#include <cstdint>

namespace unbound {

// One focused control: the Home menu rail or a dashboard card. Child pages
// keep their established controls and return to the remembered rail with B.
class DashboardNavigation {
public:
    int section() const { return section_; }
    int action() const { return action_; }
    bool menuFocused() const { return menuFocused_; }
    int gameChoice() const { return gameChoice_; }
    void leaveGameSwitch() { gameChoice_=-1; }
    void enterContent() { leaveGameSwitch();menuFocused_=false;section_=0; }
    void selectAction(int action) {
        if(action==0||action==1||action==4||action==5){enterContent();action_=action;}
    }
    void returnToMenu(int section) { leaveGameSwitch();section_=section>=0&&section<=5?section:0;menuFocused_=true; }
    bool move(std::uint32_t buttons, int activeGame=0) {
        if(gameChoice_>=0){
            const int previous=gameChoice_;
            if(buttons&(button::B|button::Down))leaveGameSwitch();
            else if(buttons&button::Left)gameChoice_=0;
            else if(buttons&button::Right)gameChoice_=1;
            return previous!=gameChoice_;
        }
        if((buttons&button::Up)&&!(buttons&button::B)&&
           (menuFocused_?section_==0:action_==0)){
            gameChoice_=activeGame;
            return true;
        }
        const auto oldSection=section_,oldAction=action_;
        const auto oldFocus=menuFocused_;
        if(buttons&button::B)menuFocused_=true;
        else if(menuFocused_){
            if(buttons&button::Right)enterContent();
            else if((buttons&button::Up)&&section_>0)--section_;
            else if((buttons&button::Down)&&section_<5)++section_;
        }else if(buttons&button::Left){
            if(action_==4)action_=1;else menuFocused_=true;
        }else if(buttons&button::Right){
            if(action_==0||action_==1)action_=4;
        }else if(buttons&button::Up){
            if(action_==1||action_==4)action_=5;else if(action_==5)action_=0;
        }else if(buttons&button::Down){
            if(action_==0)action_=5;else if(action_==5)action_=1;
        }
        return oldSection!=section_||oldAction!=action_||oldFocus!=menuFocused_;
    }
private:
    int section_=0;
    int gameChoice_=-1;
    int action_=4;
    bool menuFocused_=false;
};

} // namespace unbound
