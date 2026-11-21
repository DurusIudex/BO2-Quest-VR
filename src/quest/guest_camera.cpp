#include "guest_camera.h"
#include "camera_pose.h"
#include "aim_stabilizer.h"
#include "guest_profiles.h"
#include "render_frames.h"
#include <android/log.h>
#include <dobby.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <algorithm>
#define CAMLOG(...) __android_log_print(ANDROID_LOG_INFO,"BO2QuestVR",__VA_ARGS__)
extern "C" void quest_guest_render_entry();
extern "C" void quest_guest_setup_entry();
extern "C" void quest_guest_cmd_entry();
extern "C" void quest_guest_finish_entry();
extern "C" void quest_guest_weapon_entry();
extern "C" void quest_guest_crosshair_entry();
extern "C" void quest_guest_backend_entry();
extern "C" void quest_guest_draw3d_entry();
extern "C" void quest_guest_viewport_entry();
extern "C" void quest_guest_scissor_entry();
extern "C" void quest_guest_flame_entry();
extern "C" void quest_guest_shake_entry();
extern "C" void quest_guest_perturb_entry();
extern "C" void quest_guest_damagekick_entry();
extern "C" void quest_guest_shellshock_entry();
extern "C" uintptr_t quest_call_guest(void*,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
namespace {
void* original=nullptr;
void* originalSetup=nullptr;
uintptr_t guestBase=0;
const GuestProfile* profile=nullptr;
void* originalCmd=nullptr;void* originalFinish=nullptr;void* originalWeapon=nullptr;
void* originalCrosshair=nullptr;
void* originalBackend=nullptr;void* originalDraw3d=nullptr;
void* originalViewport=nullptr;void* originalScissor=nullptr;
void* originalFlame=nullptr;void* originalShake=nullptr;
void* originalPerturb=nullptr;void* originalDamageKick=nullptr;void* originalShellshock=nullptr;
thread_local int outputEye=-1,outputWidth=0;
bo2vr::RenderFrames pendingFrames;
thread_local bo2vr::RenderFrame activeFrame;
thread_local uint32_t activeBuffer=0;
constexpr uint32_t viewIndexOffset=0x474214,viewCountOffset=0x474218,viewArrayOffset=0x47421c,viewSize=0xc50;
uint32_t readWord(uintptr_t address){uint32_t value;memcpy(&value,reinterpret_cast<void*>(address),4);return value;}
uint32_t frontendBuffer(){return readWord(guestBase+profile->frontendData);}
uint32_t currentView(uint32_t buffer){return readWord(uintptr_t(buffer)+viewIndexOffset);}
bool aimHooks=false,aimApplied=false;
XrVector2f appliedAim{};
bo2vr::InputSample controllerInput{};
bo2vr::AimStabilizer aimStabilizer;
float aimSmoothing=0.065f;
bool physicalWeapon=true;
XrVector3f weaponGripOffset{-0.09f,0.15f,0.20f};
bo2vr::HandSample frameHand{};
std::array<unsigned char,0x78> savedEngineRef{};
float* clientAngles(){
    if(!profile)return nullptr;
    uint32_t slot=*reinterpret_cast<uint32_t*>(guestBase+profile->clientActiveGot);
    if(slot<guestBase||slot>=0xff000000u)return nullptr;
    uint32_t client=*reinterpret_cast<uint32_t*>(uintptr_t(slot));
    if(client<0x10000u||client>0xff000000u)return nullptr;
    return reinterpret_cast<float*>(uintptr_t(client)+0x2c8c);
}
std::mutex cameraLock;
std::array<XrView,2> tracked{},submitted{};
bool trackingValid=false,inGame=false,stereoEnabled=false,submittedStereo=false,anchored=false;
bool monoDiagnostic=false;
std::string diagnosticFiles;
XrPosef anchor{};
unsigned calls=0;
unsigned submittedEye=0;
std::array<unsigned char,0x78> originalRef{};
uint32_t preparedRef=0;
std::array<XrView,2> frameViews{};
bool trackedGameplay(){
    std::lock_guard<std::mutex> hold(cameraLock);
    return stereoEnabled&&inGame&&trackingValid;
}

}
namespace bo2vr {
void installGuestCamera(const std::string& files){
    if(original)return;
    diagnosticFiles=files;
    // Only the verified Beta 2.3 Zombies/Multiplayer profiles are accepted.
    // Function prologues and the executable mapping must match before writing.
    FILE* maps=fopen("/proc/self/maps","r");if(!maps)return;
    char line[1024];uintptr_t start,end;char permissions[5];
    while(fgets(line,sizeof(line),maps)){
        if(sscanf(line,"%lx-%lx %4s",&start,&end,permissions)!=3)continue;
        if(start>=UINT32_MAX || end-start<0xb33300 || permissions[0]!='r'||permissions[2]!='x')continue;
        for(const auto& candidate:guestProfiles){
            bool matches=true;
            for(const GuestFunction* fn:{&candidate.render,&candidate.setup,&candidate.view,&candidate.cell,&candidate.createCmd,&candidate.finishMove,&candidate.setAngles,&candidate.weapon,&candidate.crosshair,&candidate.backend,&candidate.draw3d,&candidate.setViewport,&candidate.setScissor,&candidate.flame,&candidate.shake,&candidate.perturb,&candidate.damageKick,&candidate.shellshockBlend})
                if(fn->offset+sizeof(fn->prologue)>end-start||memcmp(reinterpret_cast<void*>(start+fn->offset),fn->prologue,sizeof(fn->prologue)))matches=false;
            if(matches){guestBase=start;profile=&candidate;break;}
        }
        if(profile)break;
    }
    fclose(maps);
    if(!guestBase){CAMLOG("Guest camera: verified code mapping not found; no hook installed");return;}
    stereoEnabled=std::ifstream(files+"/quest_stereo.request").good();
    std::ifstream settings(files+"/quest_bindings.ini");std::string setting;
    while(std::getline(settings,setting)){
        if(setting=="render_mode=mono")monoDiagnostic=true;
        if(setting=="render_mode=panel")stereoEnabled=false;
        if(setting=="aim_smoothing=off")aimSmoothing=0;
        if(setting=="aim_smoothing=light")aimSmoothing=0.035f;
        if(setting=="aim_smoothing=strong")aimSmoothing=0.1f;
        if(setting=="weapon_tracking=rotation")physicalWeapon=false;
    }
    if(DobbyHook(reinterpret_cast<void*>(guestBase+profile->render.offset),reinterpret_cast<dobby_dummy_func_t>(quest_guest_render_entry),reinterpret_cast<dobby_dummy_func_t*>(&original))!=0){original=nullptr;CAMLOG("Guest camera hook failed");return;}
    if(DobbyHook(reinterpret_cast<void*>(guestBase+profile->setup.offset),reinterpret_cast<dobby_dummy_func_t>(quest_guest_setup_entry),reinterpret_cast<dobby_dummy_func_t*>(&originalSetup))!=0){stereoEnabled=false;CAMLOG("Early camera hook failed; stereo disabled");}
    auto hook=[&](const GuestFunction& fn,void* entry,void** out){return DobbyHook(reinterpret_cast<void*>(guestBase+fn.offset),reinterpret_cast<dobby_dummy_func_t>(entry),reinterpret_cast<dobby_dummy_func_t*>(out))==0;};
    if(!hook(profile->backend,reinterpret_cast<void*>(quest_guest_backend_entry),&originalBackend)
       ||!hook(profile->draw3d,reinterpret_cast<void*>(quest_guest_draw3d_entry),&originalDraw3d)
       ||!hook(profile->setViewport,reinterpret_cast<void*>(quest_guest_viewport_entry),&originalViewport)
       ||!hook(profile->setScissor,reinterpret_cast<void*>(quest_guest_scissor_entry),&originalScissor)){
        stereoEnabled=false;CAMLOG("Backend stereo hook failed; camera disabled");
    }
    aimHooks=hook(profile->createCmd,reinterpret_cast<void*>(quest_guest_cmd_entry),&originalCmd)
        &&hook(profile->finishMove,reinterpret_cast<void*>(quest_guest_finish_entry),&originalFinish)
        &&hook(profile->weapon,reinterpret_cast<void*>(quest_guest_weapon_entry),&originalWeapon);
    if(aimHooks&&!hook(profile->crosshair,reinterpret_cast<void*>(quest_guest_crosshair_entry),&originalCrosshair))CAMLOG("Original crosshair could not be hidden");
    if(!hook(profile->flame,reinterpret_cast<void*>(quest_guest_flame_entry),&originalFlame))CAMLOG("VR flame distortion suppression unavailable");
    if(!hook(profile->shake,reinterpret_cast<void*>(quest_guest_shake_entry),&originalShake))CAMLOG("VR camera shake suppression unavailable");
    bool comfort=hook(profile->perturb,reinterpret_cast<void*>(quest_guest_perturb_entry),&originalPerturb);
    comfort=hook(profile->damageKick,reinterpret_cast<void*>(quest_guest_damagekick_entry),&originalDamageKick)&&comfort;
    comfort=hook(profile->shellshockBlend,reinterpret_cast<void*>(quest_guest_shellshock_entry),&originalShellshock)&&comfort;
    CAMLOG("VR damage-motion and shellshock hooks installed=%d",comfort);
    CAMLOG("Guest camera hooks installed mode=%s base=%lx stereo=%d aim=%d smoothing=%.0fms",profile->mode,guestBase,stereoEnabled,aimHooks,aimSmoothing*1000);
}
void updateGuestTracking(const std::array<XrView,2>& views,bool valid,bool gameplay){
    std::lock_guard<std::mutex> hold(cameraLock);tracked=views;trackingValid=valid;inGame=gameplay;
    if(!gameplay){anchored=false;submittedStereo=false;aimStabilizer.reset();}
}
void updateGuestInput(const InputSample& input,XrTime time){
    std::lock_guard<std::mutex> hold(cameraLock);controllerInput=input;
    auto& hand=controllerInput.hands[1];
    bool valid=input.focused&&(hand.aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    hand.aim.orientation=aimStabilizer.update(hand.aim.orientation,time,valid,aimSmoothing);
}
bool guestMonoscopic(){return monoDiagnostic;}
bool guestControllerAimEnabled(){return stereoEnabled&&aimHooks;}
bool guestIntermission(){
    if(!profile)return false;
    // Same snapshot/pm_type check used by the engine's GetIsIntermission.
    const uint32_t cg=readWord(guestBase+profile->cgArray);
    if(cg<0x10000u||cg>=0xff000000u)return false;
    const uint32_t snap=readWord(uintptr_t(cg)+0x24);
    return snap>=0x10000u&&snap<0xff000000u&&readWord(uintptr_t(snap)+0x14)==6;
}
const char* guestGameMode(){return profile?profile->mode:nullptr;}
bool guestAimPose(XrPosef& head,XrPosef& aim){std::lock_guard<std::mutex> hold(cameraLock);
    if(!stereoEnabled||!aimHooks||!inGame||!trackingValid||!controllerInput.focused||(controllerInput.hands[1].aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)==0)return false;
    head=tracked[0].pose;head.position={(tracked[0].pose.position.x+tracked[1].pose.position.x)/2,(tracked[0].pose.position.y+tracked[1].pose.position.y)/2,(tracked[0].pose.position.z+tracked[1].pose.position.z)/2};aim=controllerInput.hands[1].aim;return true;
}
bool guestStereoViews(std::array<XrView,2>& views,unsigned& eye){std::lock_guard<std::mutex> hold(cameraLock);views=submitted;eye=submittedEye;return submittedStereo&&inGame&&trackingValid;}
}
extern "C" __attribute__((visibility("hidden"))) uint32_t quest_guest_setup_callback(uint32_t ref,uint32_t,uintptr_t tls){
    auto* r=reinterpret_cast<unsigned char*>(uintptr_t(ref));
    preparedRef=0;
    bool enabled;
    {
        std::lock_guard<std::mutex> hold(cameraLock);frameViews=tracked;frameHand=controllerInput.hands[1];enabled=stereoEnabled&&trackingValid&&inGame;
        if(enabled&&!anchored){anchor=frameViews[0].pose;anchor.position={(frameViews[0].pose.position.x+frameViews[1].pose.position.x)*0.5f,(frameViews[0].pose.position.y+frameViews[1].pose.position.y)*0.5f,(frameViews[0].pose.position.z+frameViews[1].pose.position.z)*0.5f};anchored=true;}
    }
    if(enabled){
        memcpy(originalRef.data(),r,originalRef.size());savedEngineRef=originalRef;preparedRef=ref;
        if(aimApplied){
            XrPosef unroll{bo2vr::aimRotation(appliedAim),{}};XrPosef identity{{0,0,0,1},{}};
            auto body=originalRef;bo2vr::applyCameraPose(body.data(),originalRef.data(),unroll,identity,{-0.8f,0.8f,0.7f,-0.7f});
            memcpy(originalRef.data()+0x44,body.data()+0x44,36);
        }
        XrPosef center=frameViews[0].pose;center.position={(frameViews[0].pose.position.x+frameViews[1].pose.position.x)*0.5f,(frameViews[0].pose.position.y+frameViews[1].pose.position.y)*0.5f,(frameViews[0].pose.position.z+frameViews[1].pose.position.z)*0.5f};
        float h=0,v=0;for(const auto& eye:frameViews){h=std::max(h,std::max(std::abs(eye.fov.angleLeft),std::abs(eye.fov.angleRight)));v=std::max(v,std::max(std::abs(eye.fov.angleUp),std::abs(eye.fov.angleDown)));}
        h=std::min(h+0.1f,1.45f);v=std::min(v+0.1f,1.45f);
        bo2vr::applyCameraPose(r,originalRef.data(),anchor,center,{-h,h,v,-v});
        if(bo2vr::guestMonoscopic())for(auto& view:frameViews){view.pose=center;view.fov={-h,h,v,-v};}
    }
    return static_cast<uint32_t>(quest_call_guest(originalSetup,tls,ref,0,0));
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_render_callback(uint32_t ref,uint32_t parms,uintptr_t tls){
    auto* r=reinterpret_cast<unsigned char*>(uintptr_t(ref));auto* v=reinterpret_cast<unsigned char*>(uintptr_t(parms));
    auto call=[&](uintptr_t f,uintptr_t a,uintptr_t b,uintptr_t c=0){return quest_call_guest(reinterpret_cast<void*>(f),tls,a,b,c);};
    if(++calls==1){
        auto* ints=reinterpret_cast<int*>(r);auto* floats=reinterpret_cast<float*>(r);
        CAMLOG("Camera sample ref=%x view=%x tls=%lx viewport=%d,%d %dx%d fovtan=%.3f,%.3f origin=%.1f,%.1f,%.1f",ref,parms,tls,ints[0],ints[1],ints[2],ints[3],floats[8],floats[9],floats[13],floats[14],floats[15]);
    }
    auto eyes=frameViews;
    if(preparedRef!=ref){call(reinterpret_cast<uintptr_t>(original),ref,parms);return;}
    if(bo2vr::guestMonoscopic()){
        call(reinterpret_cast<uintptr_t>(original),ref,parms);
        memcpy(r,savedEngineRef.data(),savedEngineRef.size());
        uint32_t buffer=frontendBuffer();
        {std::lock_guard<std::mutex> hold(cameraLock);pendingFrames.publish(buffer,{eyes,{currentView(buffer),0},1});}
        preparedRef=0;
        if(calls%300==1)CAMLOG("Monoscopic camera diagnostic frame");
        return;
    }
    // Preserve the engine's original camera and view matrices. Guest functions
    // receive the existing low-address objects, never an LP64 heap pointer.
    std::array<unsigned char,0x78> saved;std::array<unsigned char,0x150> savedView;
    saved=originalRef;memcpy(savedView.data(),v,savedView.size());
    auto* ints=reinterpret_cast<int*>(r);const int width=ints[2],height=ints[3];
    if(width<2||height<2||width>8192||height>8192){call(reinterpret_cast<uintptr_t>(original),ref,parms);return;}
    bo2vr::RenderFrame rendered;rendered.eyeCount=2;
    uint32_t buffer=frontendBuffer();
    for(unsigned eye=0;eye<2;++eye){
        memcpy(r,saved.data(),saved.size());
        // Scene targets and the bloom/film chain are full-size single-eye
        // scratch images. Pack only their final backbuffer viewport below.
        ints[0]=0;ints[1]=0;ints[2]=width;ints[3]=height;
        bo2vr::applyCameraPose(r,saved.data(),anchor,eyes[eye].pose,eyes[eye].fov);
        const float horizontal=std::max(std::abs(eyes[eye].fov.angleLeft),std::abs(eyes[eye].fov.angleRight));
        const float vertical=std::max(std::abs(eyes[eye].fov.angleUp),std::abs(eyes[eye].fov.angleDown));
        *reinterpret_cast<float*>(r+0x20)=std::tan(horizontal);*reinterpret_cast<float*>(r+0x24)=std::tan(vertical);
        eyes[eye].fov={-horizontal,horizontal,vertical,-vertical};
        call(guestBase+profile->view.offset,ref,parms);
        // R_SetViewParmsForScene clears the entire structure, including the
        // visibility-cell index. Match R_SetupRenderSceneViewParms's final step.
        *reinterpret_cast<int32_t*>(v+0x140)=static_cast<int32_t>(call(guestBase+profile->cell.offset,parms+0x100,0));
        call(reinterpret_cast<uintptr_t>(original),ref,parms);
        rendered.indices[eye]=currentView(buffer);
        uint32_t list=readWord(uintptr_t(buffer)+viewArrayOffset);
        auto* view=reinterpret_cast<unsigned char*>(uintptr_t(list)+viewSize*rendered.indices[eye]);
        // 0x150 is the render viewport, 0x160 display, 0x170 final scene
        // composition. R_SetSceneParms derives the latter from an independent
        // refdef viewport, which was still full-screen in the old stereo path.
        memcpy(view+0x170,view+0x150,16);
        // The flat game's narrower viewmodel lens distorts off-center weapons.
        // Use this eye's already-built, valid projection/VP/inverse matrices.
        // Do not zero refdef FOV values: that previously caused black gameplay.
        memcpy(view+0x980+0x40,view+0x40,0xc0);
        if(calls%300==1){
            const auto* a=reinterpret_cast<const int*>(view+0x150);
            const auto* p=reinterpret_cast<const float*>(view+0x40);
            CAMLOG("Eye %u buffer=%x index=%u viewport=%d,%d %dx%d projection=%.3f,%.3f",eye,buffer,rendered.indices[eye],a[0],a[1],a[2],a[3],p[0],p[5]);
        }
    }
    memcpy(r,savedEngineRef.data(),savedEngineRef.size());memcpy(v,savedView.data(),savedView.size());
    rendered.views=eyes;
    {std::lock_guard<std::mutex> hold(cameraLock);pendingFrames.publish(buffer,rendered);}
    preparedRef=0;
    if(calls%300==1)CAMLOG("Stereo scenes submitted, full %dx%d scratch views; packing at final output",width,height);
}

extern "C" __attribute__((visibility("hidden"))) void quest_guest_backend_callback(uint32_t buffer,uintptr_t tls){
    {
        std::lock_guard<std::mutex> hold(cameraLock);
        activeFrame=pendingFrames.take(buffer);activeBuffer=buffer;
        submitted=activeFrame.views;submittedEye=0;submittedStereo=activeFrame.eyeCount!=0;
    }
    // RB_EndFrame presents inside this call, so the compositor sees only this
    // backend buffer's poses even while the frontend prepares the next frame.
    quest_call_guest(originalBackend,tls,buffer,0,0);
    activeFrame={};activeBuffer=0;
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_draw3d_callback(uintptr_t tls){
    if(activeFrame.eyeCount!=2||!activeBuffer){quest_call_guest(originalDraw3d,tls,0,0,0);return;}
    auto* index=reinterpret_cast<uint32_t*>(uintptr_t(activeBuffer)+viewIndexOffset);
    const uint32_t saved=*index,count=readWord(uintptr_t(activeBuffer)+viewCountOffset);
    if(activeFrame.indices[0]>=count||activeFrame.indices[1]>=count){quest_call_guest(originalDraw3d,tls,0,0,0);return;}
    static unsigned draws=0;
    if(++draws%300==1)CAMLOG("Backend stereo buffer=%x global=%x views=%u indices=%u,%u",activeBuffer,readWord(guestBase+profile->backendData),count,activeFrame.indices[0],activeFrame.indices[1]);
    if(draws%60==1&&std::ifstream(diagnosticFiles+"/quest_camera_capture.request").good()){
        const uint32_t list=readWord(uintptr_t(activeBuffer)+viewArrayOffset);
        std::ofstream dump(diagnosticFiles+"/quest_camera_capture.bin",std::ios::binary);
        for(uint32_t i:activeFrame.indices)dump.write(reinterpret_cast<const char*>(uintptr_t(list)+viewSize*i),viewSize);
        dump.write(reinterpret_cast<const char*>(activeFrame.views.data()),sizeof(activeFrame.views));
        dump.close();std::remove((diagnosticFiles+"/quest_camera_capture.request").c_str());
        CAMLOG("Captured backend camera structures");
    }
    const uint32_t list=readWord(uintptr_t(activeBuffer)+viewArrayOffset);
    for(unsigned eye=0;eye<2;++eye){
        const uint32_t eyeIndex=activeFrame.indices[eye];
        outputEye=eye;outputWidth=readWord(uintptr_t(list)+viewSize*eyeIndex+0x158);
        *index=eyeIndex;quest_call_guest(originalDraw3d,tls,0,0,0);
    }
    outputEye=-1;outputWidth=0;
    *index=saved;
}

// R_GetViewport deliberately expands most scratch targets to full size.
// RB_BloomLDR also expands its final output to target 2 (the backbuffer).
// Transform only that physical viewport; leave the shader's full-eye UVs
// and 2D projection unchanged so a complete eye is scaled, never cropped.
extern "C" __attribute__((visibility("hidden"))) void quest_guest_viewport_callback(uint32_t state,uint32_t viewport,uintptr_t tls){
    auto* rect=reinterpret_cast<int32_t*>(uintptr_t(viewport));
    std::array<int32_t,4> saved;memcpy(saved.data(),rect,16);
    bool pack=outputEye>=0&&outputWidth>1&&*reinterpret_cast<unsigned char*>(uintptr_t(state)+0x534c)==2;
    if(pack){rect[0]=saved[0]/2+outputEye*(outputWidth/2);rect[2]=saved[2]/2;}
    quest_call_guest(originalViewport,tls,state,viewport,0);
    if(pack){memcpy(rect,saved.data(),16);static unsigned n=0;if(++n%600==1)CAMLOG("Packed final viewport eye=%d x=%d width=%d",outputEye,saved[0]/2+outputEye*(outputWidth/2),saved[2]/2);}
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_scissor_callback(uint32_t viewport,uintptr_t tls){
    auto* rect=reinterpret_cast<int32_t*>(uintptr_t(viewport));
    std::array<int32_t,4> saved;memcpy(saved.data(),rect,16);
    bool pack=outputEye>=0&&outputWidth>1&&*reinterpret_cast<unsigned char*>(guestBase+profile->cmdState+0x534c)==2;
    if(pack){rect[0]=saved[0]/2+outputEye*(outputWidth/2);rect[2]=saved[2]/2;}
    quest_call_guest(originalScissor,tls,viewport,0,0);
    if(pack)memcpy(rect,saved.data(),16);
}

extern "C" __attribute__((visibility("hidden"))) uint32_t quest_guest_cmd_callback(uint32_t client,uint32_t cmd,uintptr_t tls){
    if(aimApplied){if(auto* angles=clientAngles()){angles[0]-=appliedAim.x;angles[1]-=appliedAim.y;}aimApplied=false;}
    return static_cast<uint32_t>(quest_call_guest(originalCmd,tls,client,cmd,0));
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_finish_callback(uint32_t client,uint32_t cmd,uintptr_t tls){
    bool enabled;XrPosef hand{},origin{};
    {std::lock_guard<std::mutex> hold(cameraLock);enabled=aimHooks&&stereoEnabled&&inGame&&trackingValid&&anchored&&controllerInput.focused&&(controllerInput.hands[1].aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);hand=controllerInput.hands[1].aim;origin=anchor;}
    if(enabled&&!aimApplied){if(auto* angles=clientAngles()){
        appliedAim=bo2vr::aimAngles(bo2vr::product(bo2vr::inverse(origin.orientation),hand.orientation));
        if(std::isfinite(appliedAim.x)&&std::isfinite(appliedAim.y)){angles[0]+=appliedAim.x;angles[1]+=appliedAim.y;aimApplied=true;}
    }}
    quest_call_guest(originalFinish,tls,client,cmd,0);
    static unsigned aimCommands=0;
    if(enabled&&aimApplied&&cmd&&++aimCommands%300==1){
        CAMLOG("Controller shot aim pitch=%.2f yaw=%.2f command=%u,%u",appliedAim.x,appliedAim.y,readWord(uintptr_t(cmd)+0xc),readWord(uintptr_t(cmd)+0x10));
    }
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_weapon_callback(uint32_t cg,uint32_t placement,uint32_t angles,uintptr_t tls){
    quest_call_guest(originalWeapon,tls,cg,placement,angles);
    if(!preparedRef||!placement)return;
    // Rotate the entire animated gun/arms model as one rigid object. No bone
    // lengths or scale changes, and no separate hand target pulling the mesh apart.
    auto target=originalRef;
    XrPosef aim=frameHand.aim;
    aim.position={(frameViews[0].pose.position.x+frameViews[1].pose.position.x)*0.5f,(frameViews[0].pose.position.y+frameViews[1].pose.position.y)*0.5f,(frameViews[0].pose.position.z+frameViews[1].pose.position.z)*0.5f};
    if(physicalWeapon&&(frameHand.gripFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))
        aim=bo2vr::weaponCameraPose(aim,frameHand.grip.position,weaponGripOffset);
    if(frameHand.aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT){
        bo2vr::applyCameraPose(target.data(),originalRef.data(),anchor,aim,frameViews[0].fov);
    }else memcpy(target.data(),reinterpret_cast<const void*>(uintptr_t(preparedRef)),target.size());
    bo2vr::rebaseViewmodel(reinterpret_cast<unsigned char*>(uintptr_t(placement)),savedEngineRef.data(),target.data());
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_crosshair_callback(uint32_t client,uintptr_t tls){
    bool vr;
    {std::lock_guard<std::mutex> hold(cameraLock);vr=aimHooks&&stereoEnabled&&inGame&&trackingValid;}
    if(!vr)quest_call_guest(originalCrosshair,tls,client,0,0);
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_flame_callback(uint32_t view,uintptr_t tls){
    // RB_FlameFX is the full-screen burn distortion, not world fire or damage.
    if(activeFrame.eyeCount){static unsigned n=0;if(++n%600==1)CAMLOG("Suppressed VR flame screen warp");return;}
    quest_call_guest(originalFlame,tls,view,0,0);
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_shake_callback(uint32_t client,uintptr_t tls){
    if(!trackedGameplay())quest_call_guest(originalShake,tls,client,0,0);
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_perturb_callback(uint32_t cg,uintptr_t tls){
    // This function only rotates refdef axes for shellshock. Keep the tracked
    // camera rigid; the shellshock timers, audio and gameplay update elsewhere.
    if(trackedGameplay())return;
    quest_call_guest(originalPerturb,tls,cg,0,0);
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_damagekick_callback(uint32_t state,uint32_t angles,uintptr_t tls){
    // Adds damage pitch/roll to an existing view-angle accumulator; skipping it
    // must leave that accumulator intact, not zero unrelated view movement.
    if(trackedGameplay())return;
    quest_call_guest(originalDamageKick,tls,state,angles,0);
}
extern "C" __attribute__((visibility("hidden"))) void quest_guest_shellshock_callback(uint32_t client,uintptr_t tls){
    // A saved flat-screen blend contains an earlier camera pose and cannot be
    // reprojected as part of the current stereoscopic world image.
    if(trackedGameplay())return;
    quest_call_guest(originalShellshock,tls,client,0,0);
}
