#include "motion_gestures.h"
#include <cmath>
namespace bo2vr {
unsigned MotionGestures::update(InputSample& sample,XrVector3f head,XrTime time,bool gameplay){
    unsigned fired=0;
    if(!sample.focused||!gameplay){reset();for(auto& h:sample.hands)h.punch=false;return 0;}
    constexpr auto positionFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    for(unsigned i=0;i<2;++i){
        auto& hand=sample.hands[i];auto& history=hands_[i];hand.punch=false;
        if((hand.gripFlags&positionFlags)!=positionFlags || !(hand.aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)){history={};continue;}
        XrVector3f p{hand.grip.position.x-head.x,hand.grip.position.y-head.y,hand.grip.position.z-head.z};
        const float dt=float(time-history.time)*1e-9f;
        if(history.time && dt>=0.005f && dt<=0.1f){
            XrVector3f v{(p.x-history.position.x)/dt,(p.y-history.position.y)/dt,(p.z-history.position.z)/dt};
            const float speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
            const auto& q=hand.aim.orientation;
            XrVector3f forward{-2*(q.x*q.z+q.w*q.y),-2*(q.y*q.z-q.w*q.x),-1+2*(q.x*q.x+q.y*q.y)};
            const float thrust=v.x*forward.x+v.y*forward.y+v.z*forward.z;
            if(!std::isfinite(speed)||speed>10){history={};}
            else{
                if(speed<0.65f)history.armed=true;
                if(history.armed && speed>1.8f && thrust>1.4f && (!history.lastPunch || time-history.lastPunch>400000000)){
                    history.armed=false;history.lastPunch=time;history.pulseUntil=time+80000000;fired|=1u<<i;
                }
            }
        }else{history={};}
        history.position=p;history.time=time;hand.punch=time<history.pulseUntil;
    }
    return fired;
}
}
