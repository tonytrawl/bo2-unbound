#include "inherited_repair_gate.hpp"
#include <cassert>
#include <iostream>
int main(){
    unbound::InheritedRepairGate gate;
    assert(!gate.blocked());
    gate.observe("usb:usa",true,true,"Original files need restoration.");
    assert(gate.blocked());
    gate.observe("usb:usa",false,false,"Source unavailable.");
    assert(gate.blocked()&&gate.detail()=="Source unavailable.");
    gate.observe("mlc:europe",true,false,"");
    assert(!gate.blocked()&&gate.target()=="mlc:europe");
    gate.observe("usb:usa",true,true,"Repair paused.");
    assert(gate.blocked());
    gate.observe("usb:usa",true,false,"");
    assert(!gate.blocked());
    gate.clear();assert(!gate.blocked()&&gate.target().empty());
    std::cout<<"Inherited launch gate: current marker, errors, destinations and completion passed\n";
}
