#include "camera_pose.h"
#include "render_frames.h"
#include "aim_stabilizer.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace bo2vr;
bool near(float a,float b){return std::abs(a-b)<0.0001f;}
int main(){
    std::array<unsigned char,0x78> original{};
    const float basis[9]={1,0,0,0,1,0,0,0,1};
    memcpy(original.data()+0x44,basis,sizeof(basis));
    // Fields adjoining the packed axes must remain untouched.
    for(unsigned i=0x68;i<original.size();++i)original[i]=0x5a;
    XrPosef anchor{{0,0,0,1},{0,0,0}};
    XrFovf fov{-0.8f,0.8f,0.7f,-0.7f};
    auto out=original;
    applyCameraPose(out.data(),original.data(),anchor,anchor,fov);
    assert(memcmp(out.data()+0x44,basis,sizeof(basis))==0);
    assert(memcmp(out.data()+0x68,original.data()+0x68,0x10)==0);
    const float halfPi=1.57079632679f;
    for(int axis=0;axis<3;++axis)for(int degrees=-170;degrees<=170;degrees+=10){
        float s=std::sin(degrees*halfPi/180),c=std::cos(degrees*halfPi/180);
        XrPosef p=anchor;p.orientation={axis==0?s:0,axis==1?s:0,axis==2?s:0,c};
        applyCameraPose(out.data(),original.data(),anchor,p,fov);
        float rows[3][3];memcpy(rows,out.data()+0x44,sizeof(rows));
        // A rotation must preserve lengths and perpendicularity, including roll.
        for(int a=0;a<3;++a)for(int b=0;b<3;++b){
            float dot=0;for(int k=0;k<3;++k)dot+=rows[a][k]*rows[b][k];
            assert(near(dot,a==b?1.f:0.f));
        }
        assert(memcmp(out.data()+0x68,original.data()+0x68,0x10)==0);
    }
    XrPosef translated=anchor;translated.position={1,2,-3};
    applyCameraPose(out.data(),original.data(),anchor,translated,fov);
    float position[3];memcpy(position,out.data()+0x34,sizeof(position));
    assert(near(position[0],3*39.3700787f));assert(near(position[1],-39.3700787f));assert(near(position[2],2*39.3700787f));
    // Initial headset orientation is neutral relative to its recorded anchor.
    anchor.orientation={0,std::sin(0.6f),0,std::cos(0.6f)};
    applyCameraPose(out.data(),original.data(),anchor,anchor,fov);
    float rotated[9];memcpy(rotated,out.data()+0x44,sizeof(rotated));
    for(int i=0;i<9;++i)assert(near(rotated[i],basis[i]));
    for(float pitch:{-80.f,-30.f,0.f,45.f,80.f})for(float yaw:{-170.f,-90.f,0.f,65.f,170.f}){
        auto measured=aimAngles(aimRotation({pitch,yaw}));assert(near(measured.x,pitch));assert(near(measured.y,yaw));
        XrPosef aiming{aimRotation({pitch,yaw}),{}};
        auto aimed=original;XrPosef neutral{{0,0,0,1},{}};
        applyCameraPose(aimed.data(),original.data(),neutral,aiming,fov);
        auto body=aimed;applyCameraPose(body.data(),aimed.data(),aiming,neutral,fov);
        float restored[9];memcpy(restored,body.data()+0x44,sizeof(restored));
        for(int i=0;i<9;++i)assert(near(restored[i],basis[i]));
        float weaponBasis[3][3];memcpy(weaponBasis,aimed.data()+0x44,sizeof(weaponBasis));
        auto q=basisQuaternion(weaponBasis);auto forward=rotate(q,{1,0,0});
        assert(near(forward.x,weaponBasis[0][0])&&near(forward.y,weaponBasis[0][1])&&near(forward.z,weaponBasis[0][2]));
    }
    std::cout<<"Camera pose layout and rotation tests passed\n";
    // A default viewmodel keeps its local animated offset when the shot camera
    // points elsewhere. Rebase forward and back across pitch/yaw and translation.
    for(float pitch:{-60.f,0.f,60.f})for(float yaw:{-120.f,0.f,120.f}){
        XrPosef neutral{{0,0,0,1},{}},aiming{aimRotation({pitch,yaw}),{0.2f,-0.1f,0.3f}};
        auto target=original;applyCameraPose(target.data(),original.data(),neutral,aiming,fov);
        std::array<unsigned char,32> model{};XrQuaternionf q{0,0,0,1};XrVector3f p{10,-3,-4};float scale=1;
        memcpy(model.data(),&q,16);memcpy(model.data()+16,&p,12);memcpy(model.data()+28,&scale,4);
        rebaseViewmodel(model.data(),original.data(),target.data());
        float targetBasis[3][3];XrVector3f targetOrigin,result;memcpy(targetBasis,target.data()+0x44,36);memcpy(&targetOrigin,target.data()+0x34,12);memcpy(&result,model.data()+16,12);
        XrVector3f local{result.x-targetOrigin.x,result.y-targetOrigin.y,result.z-targetOrigin.z};
        assert(near(local.x*targetBasis[0][0]+local.y*targetBasis[0][1]+local.z*targetBasis[0][2],10));
        assert(near(local.x*targetBasis[1][0]+local.y*targetBasis[1][1]+local.z*targetBasis[1][2],-3));
        assert(near(local.x*targetBasis[2][0]+local.y*targetBasis[2][1]+local.z*targetBasis[2][2],-4));
        rebaseViewmodel(model.data(),target.data(),original.data());
        memcpy(&result,model.data()+16,12);memcpy(&q,model.data(),16);
        assert(near(result.x,10)&&near(result.y,-3)&&near(result.z,-4));
        assert(near(q.x,0)&&near(q.y,0)&&near(q.z,0)&&near(std::abs(q.w),1));
        assert(memcmp(model.data()+28,&scale,4)==0);
    }
    std::cout<<"Default viewmodel placement tests passed\n";
    // Moving the controller translates the whole model equally. Head position
    // is not an input, so leaning independently cannot drag the physical gun.
    XrPosef hand{aimRotation({-20,75}),{}};XrVector3f grip{0.3f,1.2f,-0.4f},offset{-0.09f,0.15f,0.20f};
    auto gunA=weaponCameraPose(hand,grip,offset);
    grip.x+=0.12f;grip.y-=0.08f;grip.z+=0.2f;
    auto gunB=weaponCameraPose(hand,grip,offset);
    assert(near(gunB.position.x-gunA.position.x,0.12f)&&near(gunB.position.y-gunA.position.y,-0.08f)&&near(gunB.position.z-gunA.position.z,0.2f));
    assert(memcmp(&gunA.orientation,&gunB.orientation,sizeof(XrQuaternionf))==0);
    for(int hz:{60,72,90,120}){
        AimStabilizer smoother;int64_t time=1000000000,step=1000000000/hz;
        smoother.update(aimRotation({0,0}),time,true,0.065f);
        float rawEnergy=0,filteredEnergy=0;
        for(int i=0;i<hz*2;++i){
            float jitter=(i%2?1.f:-1.f)*0.5f;time+=step;
            auto q=smoother.update(aimRotation({0,jitter}),time,true,0.065f);
            float yaw=aimAngles(q).y;
            if(i>hz){rawEnergy+=jitter*jitter;filteredEnergy+=yaw*yaw;}
        }
        assert(filteredEnergy<rawEnergy*0.15f);
        XrQuaternionf q{};
        for(int i=0;i<hz/5;++i){time+=step;q=smoother.update(aimRotation({0,60}),time,true,0.065f);}
        assert(std::abs(aimAngles(q).y-60)<1.5f);
        // Equivalent +/- quaternions cannot produce a spin or cancel to zero.
        q={-q.x,-q.y,-q.z,-q.w};time+=step;
        q=smoother.update(q,time,true,0.065f);assert(std::abs(aimAngles(q).y-60)<1.5f);
        smoother.update(q,time+step,false,0.065f);
        q=smoother.update(aimRotation({0,-45}),time+2*step,true,0.065f);assert(near(aimAngles(q).y,-45));
        q=smoother.update(aimRotation({0,20}),time+3*step,true,0);assert(near(aimAngles(q).y,20));
    }
    std::cout<<"Aim jitter suppression, fast turn, quaternion sign and focus reset tests passed\n";
    RenderFrames frames;
    RenderFrame older,newer;older.eyeCount=newer.eyeCount=2;
    older.views[0].pose.position.x=1;older.views[1].pose.position.x=2;
    newer.views[0].pose.position.x=10;newer.views[1].pose.position.x=20;
    older.indices={0,1};newer.indices={2,3};
    frames.publish(0x1000,older);frames.publish(0x2000,newer);
    // The renderer consumes an older buffer after a newer pose is available.
    auto actual=frames.take(0x1000);
    assert(actual.eyeCount==2&&actual.views[0].pose.position.x==1&&actual.views[1].pose.position.x==2);
    assert(actual.indices[0]==0&&actual.indices[1]==1);
    assert(frames.take(0x1000).eyeCount==0); // no stale stereo in a reused menu buffer
    assert(frames.take(0x3000).eyeCount==0); // never guess a different buffer's pose
    actual=frames.take(0x2000);assert(actual.views[0].pose.position.x==10&&actual.views[1].pose.position.x==20);
    frames.publish(0x1000,newer);actual=frames.take(0x1000);assert(actual.indices[1]==3);
    std::cout<<"Backend frame/pose association tests passed\n";
}
