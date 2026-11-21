#include "motion_gestures.h"
#include "controller_bindings.h"
#include <cassert>
#include <iostream>
using namespace bo2vr;
int main(){
    MotionGestures g;InputSample s;s.focused=true;XrTime t=1000000000;
    for(auto& h:s.hands){h.gripFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;h.aimFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;}
    assert(g.update(s,{},t,true)==0);
    s.hands[0].grip.position.z=-0.04f;t+=16000000;
    assert(g.update(s,{},t,true)==1);assert(s.hands[0].punch&&!s.hands[1].punch);
    assert(ControllerBindings().map(s).buttons==128);
    s.hands[0].grip.position.z=-0.08f;t+=16000000;assert(g.update(s,{},t,true)==0);
    t+=100000000;g.update(s,{},t,true);assert(!s.hands[0].punch);
    g.reset();s.hands[0].grip.position={};g.update(s,{},t,true);
    s.hands[0].grip.position.z=s.hands[1].grip.position.z=-0.06f;t+=16000000;
    assert(g.update(s,{0,0,-0.06f},t,true)==0); // Body translation is not a punch.
    s.hands[1].grip.position.z=-0.10f;t+=16000000;assert(g.update(s,{0,0,-0.06f},t,true)==2);
    s.focused=false;t+=16000000;assert(g.update(s,{},t,true)==0);assert(!s.hands[1].punch);
    s.focused=true;s.hands[1].gripFlags=0;s.hands[1].grip.position.z=-5;t+=16000000;assert(g.update(s,{},t,true)==0);
    assert(g.update(s,{},t,false)==0);
    std::cout<<"Punch gesture tests passed\n";
}
