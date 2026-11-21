#pragma once
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bo2vr {
inline constexpr float radiansPerDegree=0.017453292519943295f;
inline XrVector3f rotate(XrQuaternionf q,XrVector3f v){
    XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
inline XrQuaternionf product(XrQuaternionf a,XrQuaternionf b){return {
    a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline XrQuaternionf inverse(XrQuaternionf q){return {-q.x,-q.y,-q.z,q.w};}
inline XrVector2f aimAngles(XrQuaternionf relative){
    auto f=rotate(relative,{0,0,-1});
    return {-std::asin(std::clamp(f.y,-1.f,1.f))/radiansPerDegree,std::atan2(-f.x,-f.z)/radiansPerDegree};
}
inline XrQuaternionf aimRotation(XrVector2f angles){
    float y=angles.y*radiansPerDegree/2,p=-angles.x*radiansPerDegree/2;
    return product({0,std::sin(y),0,std::cos(y)},{std::sin(p),0,0,std::cos(p)});
}
inline XrQuaternionf basisQuaternion(const float basis[3][3]){
    // Basis rows are the model's world-space X/Y/Z axes (matrix columns).
    float m[3][3];for(int i=0;i<3;++i)for(int j=0;j<3;++j)m[i][j]=basis[j][i];
    XrQuaternionf q{};float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0){float s=2*std::sqrt(trace+1);q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4};}
    else {int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;
        float s=2*std::sqrt(std::max(0.f,1+m[i][i]-m[j][j]-m[k][k]));if(s<0.0001f)return {0,0,0,1};
        float v[3]{};v[i]=s/4;v[j]=(m[j][i]+m[i][j])/s;v[k]=(m[k][i]+m[i][k])/s;
        q={v[0],v[1],v[2],(m[k][j]-m[j][k])/s};}
    return q;
}
inline void applyCameraPose(unsigned char* r,const unsigned char* saved,XrPosef anchor,XrPosef pose,XrFovf fov){
    // Verified against R_SetViewParmsForScene: forward at 0x44, left at
    // 0x50, up at 0x5c. These are packed vec3s, NOT 16-byte SIMD rows.
    float basis[3][3],position[3];
    memcpy(position,saved+0x34,sizeof(position));
    memcpy(basis,saved+0x44,sizeof(basis));
    XrQuaternionf inverse{-anchor.orientation.x,-anchor.orientation.y,-anchor.orientation.z,anchor.orientation.w};
    auto relative=product(inverse,pose.orientation);
    XrVector3f offset{pose.position.x-anchor.position.x,pose.position.y-anchor.position.y,pose.position.z-anchor.position.z};
    offset=rotate(inverse,offset);
    const XrVector3f axes[]={rotate(relative,{0,0,-1}),rotate(relative,{-1,0,0}),rotate(relative,{0,1,0})};
    auto world=[&](XrVector3f a,int component){return -a.z*basis[0][component]-a.x*basis[1][component]+a.y*basis[2][component];};
    float transformed[3][3];
    for(int component=0;component<3;++component){
        position[component]+=world(offset,component)*39.3700787f;
        for(int row=0;row<3;++row)transformed[row][component]=world(axes[row],component);
    }
    memcpy(r+0x34,position,sizeof(position));
    memcpy(r+0x44,transformed,sizeof(transformed));
    float horizontal=std::tan(std::max(std::abs(fov.angleLeft),std::abs(fov.angleRight)));
    float vertical=std::tan(std::max(std::abs(fov.angleUp),std::abs(fov.angleDown)));
    memcpy(r+0x20,&horizontal,sizeof(float));memcpy(r+0x24,&vertical,sizeof(float));
}
inline void rebaseViewmodel(unsigned char* placement,const unsigned char* from,const unsigned char* to){
    float fromBasis[3][3],toBasis[3][3];XrVector3f fromPosition,toPosition,position;XrQuaternionf orientation;
    memcpy(fromBasis,from+0x44,36);memcpy(toBasis,to+0x44,36);
    memcpy(&fromPosition,from+0x34,12);memcpy(&toPosition,to+0x34,12);
    memcpy(&position,placement+0x10,12);memcpy(&orientation,placement,16);
    auto delta=product(basisQuaternion(toBasis),inverse(basisQuaternion(fromBasis)));
    auto offset=rotate(delta,{position.x-fromPosition.x,position.y-fromPosition.y,position.z-fromPosition.z});
    position={toPosition.x+offset.x,toPosition.y+offset.y,toPosition.z+offset.z};
    orientation=product(delta,orientation);
    memcpy(placement,&orientation,16);memcpy(placement+0x10,&position,12);
}
inline XrPosef weaponCameraPose(XrPosef aim,XrVector3f gripPosition,XrVector3f gripOffset){
    // Translate the original camera-relative model root to the controller grip.
    // Offset is in controller coordinates, in metres; never scale the skeleton.
    auto offset=rotate(aim.orientation,gripOffset);
    aim.position={gripPosition.x+offset.x,gripPosition.y+offset.y,gripPosition.z+offset.z};
    return aim;
}
}
