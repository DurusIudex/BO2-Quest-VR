#include "controller_bindings.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <iostream>
using namespace bo2vr;
int main(){
    ControllerBindings b;
    InputSample s; s.focused=true;
    s.hands[0].trigger=0.5f;s.hands[1].trigger=1;
    s.hands[1].primary=true;s.hands[0].menu=true;
    auto p=b.map(s);assert(p.leftTrigger==127 && p.rightTrigger==255 && p.buttons==(4096|16));
    s.focused=false;p=b.map(s);assert(p.buttons==0 && p.rightTrigger==0 && p.leftTrigger==0);
    s={};s.focused=true;s.hands[0].stick={0.1f,0};assert(b.map(s).lx==0);
    s.hands[0].stick={1,0};assert(b.map(s).lx==32767);
    s.hands[0].stick={1,1};p=b.map(s);assert(p.lx>23000 && p.lx<23200 && p.ly==p.lx);
    s.hands[0].stick={std::numeric_limits<float>::quiet_NaN(),1};assert(b.map(s).ly==0);
    const char* path="controller_bindings_test.tmp";
    {std::ofstream f(path);f<<"a=fire\nright_trigger=none\nleft_grip=jump\nswap_sticks=true\ndeadzone=0.25\nx=unknown\n";}
    assert(b.load(path));std::remove(path);
    s={};s.focused=true;s.hands[1].primary=true;s.hands[0].squeeze=0.54f;s.hands[1].stick={0,1};
    p=b.map(s);assert(p.rightTrigger==255 && p.buttons==0 && p.ly==32767 && p.ry==0);
    s.hands[0].squeeze=0.55f;s.hands[0].primary=true;p=b.map(s);assert(p.buttons==(4096|16384));
    {std::ofstream f(path);f<<"deadzone=nan\n";}assert(b.load(path));assert(b.deadzone==0.15f);std::remove(path);
    {std::ofstream f(path);f<<"deadzone=9\n";}assert(b.load(path));assert(b.deadzone==0.5f);std::remove(path);
    assert(!b.load("missing-bindings-file.ini"));
    std::cout<<"Controller remapping tests passed\n";
}
