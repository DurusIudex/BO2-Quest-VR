#pragma once
#include "camera_pose.h"
#include <cstdint>

namespace bo2vr {
// Smooth orientation once per XR sample, shared by shots, reticle and viewmodel.
// Large deliberate turns reduce the time constant; small tremors get more damping.
class AimStabilizer {
    XrQuaternionf filtered{0,0,0,1};
    int64_t previous=0;
    bool ready=false;
public:
    void reset(){ready=false;previous=0;}
    XrQuaternionf update(XrQuaternionf input,int64_t time,bool valid,float seconds){
        float norm=std::sqrt(input.x*input.x+input.y*input.y+input.z*input.z+input.w*input.w);
        if(!valid||!std::isfinite(norm)||norm<0.001f){reset();return {0,0,0,1};}
        input={input.x/norm,input.y/norm,input.z/norm,input.w/norm};
        const double dt=double(time-previous)*1e-9;
        if(!ready||dt<=0||dt>0.25||seconds<=0){filtered=input;previous=time;ready=true;return filtered;}
        previous=time;
        float dot=filtered.x*input.x+filtered.y*input.y+filtered.z*input.z+filtered.w*input.w;
        if(dot<0){input={-input.x,-input.y,-input.z,-input.w};dot=-dot;}
        float angle=2*std::acos(std::clamp(dot,0.f,1.f));
        float tau=std::max(0.008f,seconds/(1+12*angle));
        float alpha=1-std::exp(-float(dt)/tau);
        filtered={filtered.x+alpha*(input.x-filtered.x),filtered.y+alpha*(input.y-filtered.y),filtered.z+alpha*(input.z-filtered.z),filtered.w+alpha*(input.w-filtered.w)};
        norm=std::sqrt(filtered.x*filtered.x+filtered.y*filtered.y+filtered.z*filtered.z+filtered.w*filtered.w);
        filtered={filtered.x/norm,filtered.y/norm,filtered.z/norm,filtered.w/norm};
        return filtered;
    }
};
}
