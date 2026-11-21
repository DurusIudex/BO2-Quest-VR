#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#define VK_USE_PLATFORM_ANDROID_KHR
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/log.h>
#include <dlfcn.h>
#include <dobby.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include "controller_bindings.h"
#include "guest_camera.h"
#include "motion_gestures.h"
#include "relay_transport.h"
#include "camera_pose.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"BO2QuestVR",__VA_ARGS__)
namespace {
JavaVM* vm = nullptr;
jobject activity = nullptr;
std::string files;
thread_local bool insideRuntime = false;
struct RuntimeCall { bool old=insideRuntime; RuntimeCall(){insideRuntime=true;} ~RuntimeCall(){insideRuntime=old;} };
std::recursive_mutex lock;
XrInstance xrInstance = XR_NULL_HANDLE;
XrSystemId xrSystem = XR_NULL_SYSTEM_ID;
XrSession session = XR_NULL_HANDLE;
XrSpace localSpace = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
bool running = false, frameOpen = false;
PFN_xrPerfSettingsSetPerformanceLevelEXT setPerformance=nullptr;
bo2vr::QuestInput input;
bo2vr::ControllerBindings bindings;
bo2vr::MotionGestures gestures;
XrSwapchain xrSwap = XR_NULL_HANDLE;
XrSwapchain reticleSwap=XR_NULL_HANDLE;
bool reticleFailed=false;
std::vector<XrSwapchainImageVulkanKHR> xrImages;
uint32_t xrWidth=0,xrHeight=0;
VkFormat xrFormat=VK_FORMAT_UNDEFINED;
struct EyeCapture {XrSwapchain swap=XR_NULL_HANDLE;std::vector<XrSwapchainImageVulkanKHR> images;bool ready=false;XrView view{XR_TYPE_VIEW};};
constexpr bool sameFrameStereo=true;
std::array<EyeCapture,2> eyeCaptures;
XrPosef menuPose{{0,0,0,1},{0,0,-2.5f}};
bool menuAnchored=false,wasGameplay=false;
VkInstance vkInstance=VK_NULL_HANDLE;
VkDevice device=VK_NULL_HANDLE;
VkQueue gameQueue=VK_NULL_HANDLE;
VkCommandPool commandPool=VK_NULL_HANDLE;
VkCommandBuffer copyCommand=VK_NULL_HANDLE;
VkSemaphore copyDone=VK_NULL_HANDLE;
VkFence copyFence=VK_NULL_HANDLE;
struct DeviceInfo { VkPhysicalDevice physical; uint32_t queueFamily; };
std::map<VkDevice,DeviceInfo> devices;
struct SwapInfo { VkDevice device; VkExtent2D extent; VkFormat format; std::vector<VkImage> images; };
std::map<VkSwapchainKHR,SwapInfo> swaps;
PFN_vkGetInstanceProcAddr realGipa=nullptr;
PFN_vkGetDeviceProcAddr realGdpa=nullptr;
PFN_vkCreateInstance realCreateInstance=nullptr;
PFN_vkCreateDevice realCreateDevice=nullptr;
PFN_vkCreateSwapchainKHR realCreateSwapchain=nullptr;
PFN_vkQueuePresentKHR realPresent=nullptr;
PFN_vkDestroyDevice realDestroyDevice=nullptr;
PFN_vkDestroySwapchainKHR realDestroySwapchain=nullptr;
using PadFn=void(*)(JNIEnv*,jclass,jboolean,jint,jint,jint,jint,jint,jint,jint);
PadFn sendPad=nullptr;
using StateFn=jint(*)(JNIEnv*,jclass);
StateFn readClient=nullptr;
bool gameplay=false;
unsigned long long frames=0;
using Clock=std::chrono::steady_clock;
Clock::time_point previousFrame{};
std::vector<double> frameTimes;
double maximumWait=0,maximumCopyWait=0;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}

void check(XrResult r,const char* call){if(XR_FAILED(r)){LOG("%s failed: %d",call,r);throw std::runtime_error(call);}}
void checkVk(VkResult r,const char* call){if(r!=VK_SUCCESS){LOG("%s failed: %d",call,r);throw std::runtime_error(call);}}
template<class T>T xrFunction(const char* name){PFN_xrVoidFunction f=nullptr;check(xrGetInstanceProcAddr(xrInstance,name,&f),name);return reinterpret_cast<T>(f);}
template<class T>T vkFunction(const char* name){auto f=realGdpa(device,name);if(!f)throw std::runtime_error(name);return reinterpret_cast<T>(f);}
#define VF(name) vkFunction<PFN_##name>(#name)
// Opt-in diagnostic only: preserve the game's pixels before XR reprojection.
struct SourceCapture {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    bool pending=false;
    ~SourceCapture(){
        if(pending)VF(vkWaitForFences)(device,1,&copyFence,VK_TRUE,UINT64_MAX);
        if(buffer)VF(vkDestroyBuffer)(device,buffer,nullptr);
        if(memory)VF(vkFreeMemory)(device,memory,nullptr);
    }
    void record(VkImage source,VkFormat format){
        if(frames%60!=0||!std::ifstream(files+"/quest_capture.request").good())return;
        if(format!=VK_FORMAT_R8G8B8A8_UNORM&&format!=VK_FORMAT_R8G8B8A8_SRGB&&format!=VK_FORMAT_B8G8R8A8_UNORM&&format!=VK_FORMAT_B8G8R8A8_SRGB)return;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=VkDeviceSize(xrWidth)*xrHeight*4;info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        checkVk(VF(vkCreateBuffer)(device,&info,nullptr,&buffer),"capture buffer");
        VkMemoryRequirements requirements{};VF(vkGetBufferMemoryRequirements)(device,buffer,&requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        auto getMemory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(realGipa(vkInstance,"vkGetPhysicalDeviceMemoryProperties"));
        getMemory(devices.at(device).physical,&properties);
        uint32_t type=properties.memoryTypeCount;
        constexpr VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
        if(type==properties.memoryTypeCount)throw std::runtime_error("capture memory type");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
        checkVk(VF(vkAllocateMemory)(device,&allocation,nullptr,&memory),"capture allocate");
        checkVk(VF(vkBindBufferMemory)(device,buffer,memory,0),"capture bind");
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={xrWidth,xrHeight,1};
        VF(vkCmdCopyImageToBuffer)(copyCommand,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffer;barrier.size=VK_WHOLE_SIZE;
        VF(vkCmdPipelineBarrier)(copyCommand,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    }
    void save(VkFormat format){
        if(!buffer)return;
        checkVk(VF(vkWaitForFences)(device,1,&copyFence,VK_TRUE,UINT64_MAX),"capture wait");pending=false;
        void* mapped=nullptr;checkVk(VF(vkMapMemory)(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"capture map");
        std::ofstream output(files+"/quest_capture.ppm",std::ios::binary);output<<"P6\n"<<xrWidth<<" "<<xrHeight<<"\n255\n";
        const auto* pixels=static_cast<const unsigned char*>(mapped);
        const bool bgra=format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_B8G8R8A8_SRGB;
        for(size_t i=0;i<size_t(xrWidth)*xrHeight;++i){char rgb[3]={char(pixels[i*4+(bgra?2:0)]),char(pixels[i*4+1]),char(pixels[i*4+(bgra?0:2)])};output.write(rgb,3);}
        output.close();VF(vkUnmapMemory)(device,memory);
        std::remove((files+"/quest_capture.request").c_str());LOG("Captured raw game surface %ux%u format=%d",xrWidth,xrHeight,format);
    }
};
void pad(const bo2vr::GamepadOutput& state,bool active){
    if(!sendPad){void* host=dlopen("libt6host.so",RTLD_NOW|RTLD_NOLOAD);if(host){sendPad=reinterpret_cast<PadFn>(dlsym(host,"Java_org_wiiucoop_bo2android_T6Native_nativeSetTouchPad"));readClient=reinterpret_cast<StateFn>(dlsym(host,"Java_org_wiiucoop_bo2android_T6Native_nativeClientState"));dlclose(host);}}
    if(!sendPad)return;
    JNIEnv* env=nullptr;
    bool attached=vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK;
    if(attached && vm->AttachCurrentThread(&env,nullptr)!=JNI_OK)return;
    sendPad(env,nullptr,active,state.buttons,state.leftTrigger,state.rightTrigger,state.lx,state.ly,state.rx,state.ry);
    if(readClient){int client=readClient(env,nullptr);bool next=client>=0 && (client&127)==10 && (client&128)!=0 && !bo2vr::guestIntermission();if(next!=gameplay)LOG("Presentation changed to %s",next?"VR gameplay":"stationary menu/stats");gameplay=next;}
    if(attached)vm->DetachCurrentThread();
}
void destroySession(){
    RuntimeCall guard;
    pad({},false);
    input.shutdown();
    if(device)VF(vkDeviceWaitIdle)(device);
    if(xrSwap)xrDestroySwapchain(xrSwap);
    if(reticleSwap)xrDestroySwapchain(reticleSwap);reticleSwap=XR_NULL_HANDLE;reticleFailed=false;
    for(auto& eye:eyeCaptures){if(eye.swap)xrDestroySwapchain(eye.swap);eye={};}
    xrSwap=XR_NULL_HANDLE;xrImages.clear();
    if(localSpace)xrDestroySpace(localSpace);if(viewSpace)xrDestroySpace(viewSpace);
    localSpace=viewSpace=XR_NULL_HANDLE;
    if(session)xrDestroySession(session);
    session=XR_NULL_HANDLE;running=false;frameOpen=false;
    menuAnchored=false;gestures.reset();
    if(device && commandPool)VF(vkDestroyCommandPool)(device,commandPool,nullptr);
    if(device && copyDone)VF(vkDestroySemaphore)(device,copyDone,nullptr);
    if(device && copyFence)VF(vkDestroyFence)(device,copyFence,nullptr);
    commandPool=VK_NULL_HANDLE;copyCommand=VK_NULL_HANDLE;copyDone=VK_NULL_HANDLE;copyFence=VK_NULL_HANDLE;device=VK_NULL_HANDLE;
}
void makeSession(VkDevice dev,const SwapInfo& swap){
    if(session)return;
    RuntimeCall guard;
    device=dev;
    auto info=devices.at(dev);
    VF(vkGetDeviceQueue)(dev,info.queueFamily,0,&gameQueue);
    XrGraphicsBindingVulkanKHR graphics{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    graphics.instance=vkInstance;graphics.physicalDevice=info.physical;graphics.device=dev;graphics.queueFamilyIndex=info.queueFamily;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};create.next=&graphics;create.systemId=xrSystem;
    check(xrCreateSession(xrInstance,&create,&session),"xrCreateSession");
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.poseInReferenceSpace.orientation.w=1;
    space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;check(xrCreateReferenceSpace(session,&space,&localSpace),"local space");
    space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;check(xrCreateReferenceSpace(session,&space,&viewSpace),"view space");
    check(input.initialize(xrInstance,session),"controller actions");
    bindings.load(files+"/quest_bindings.ini");
    uint32_t count=0;check(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"format count");
    std::vector<int64_t> formats(count);check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"formats");
    if(std::find(formats.begin(),formats.end(),int64_t(swap.format))==formats.end())throw std::runtime_error("game format unsupported by OpenXR");
    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};sci.format=swap.format;sci.sampleCount=1;sci.width=swap.extent.width;sci.height=swap.extent.height;sci.faceCount=1;sci.arraySize=1;sci.mipCount=1;
    sci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    check(xrCreateSwapchain(session,&sci,&xrSwap),"xrCreateSwapchain");
    xrWidth=sci.width;xrHeight=sci.height;xrFormat=swap.format;
    check(xrEnumerateSwapchainImages(xrSwap,0,&count,nullptr),"image count");
    xrImages.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(xrEnumerateSwapchainImages(xrSwap,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data())),"images");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=info.queueFamily;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    checkVk(VF(vkCreateCommandPool)(device,&pool,nullptr,&commandPool),"command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=commandPool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    checkVk(VF(vkAllocateCommandBuffers)(device,&allocate,&copyCommand),"copy command");
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};checkVk(VF(vkCreateSemaphore)(device,&sem,nullptr,&copyDone),"copy semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(VF(vkCreateFence)(device,&fence,nullptr,&copyFence),"copy fence");
    LOG("OpenXR session and controller actions created; capture %ux%u",xrWidth,xrHeight);
    bo2vr::installGuestCamera(files);
    bo2vr::installRelay(files,bo2vr::guestGameMode());
}
void resizeCapture(const SwapInfo& swap){
    if(xrWidth==swap.extent.width && xrHeight==swap.extent.height && xrFormat==swap.format)return;
    checkVk(VF(vkDeviceWaitIdle)(device),"resize wait");
    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};sci.format=swap.format;sci.sampleCount=1;sci.width=swap.extent.width;sci.height=swap.extent.height;sci.faceCount=1;sci.arraySize=1;sci.mipCount=1;
    sci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    XrSwapchain next=XR_NULL_HANDLE;
    check(xrCreateSwapchain(session,&sci,&next),"resize capture");
    uint32_t count=0;
    try{
        check(xrEnumerateSwapchainImages(next,0,&count,nullptr),"resize image count");
        std::vector<XrSwapchainImageVulkanKHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        check(xrEnumerateSwapchainImages(next,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"resize images");
        check(xrDestroySwapchain(xrSwap),"retire capture");
        xrSwap=next;xrImages=std::move(images);xrWidth=sci.width;xrHeight=sci.height;xrFormat=swap.format;
        for(auto& eye:eyeCaptures){if(eye.swap)xrDestroySwapchain(eye.swap);eye={};}
        LOG("Capture resized to %ux%u",xrWidth,xrHeight);
    }catch(...){xrDestroySwapchain(next);throw;}
}
void createEyeCaptures(){
    for(auto& eye:eyeCaptures){
        if(eye.swap)continue;
        XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};sci.format=xrFormat;sci.sampleCount=1;sci.width=xrWidth;sci.height=xrHeight;sci.faceCount=1;sci.arraySize=1;sci.mipCount=1;
        sci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        check(xrCreateSwapchain(session,&sci,&eye.swap),"eye capture");uint32_t count=0;
        check(xrEnumerateSwapchainImages(eye.swap,0,&count,nullptr),"eye image count");
        eye.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        check(xrEnumerateSwapchainImages(eye.swap,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"eye images");
    }
}
void events(){
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(xrInstance,&event)==XR_SUCCESS){
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            auto* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            sessionState=changed->state;LOG("OpenXR state %d",sessionState);
            if(sessionState==XR_SESSION_STATE_READY&&!running){
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;check(xrBeginSession(session,&begin),"xrBeginSession");running=true;
                if(setPerformance){
                    auto cpu=setPerformance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                    auto gpu=setPerformance(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                    LOG("Sustained performance request CPU=%d GPU=%d",cpu,gpu);
                }
            }
            if(sessionState==XR_SESSION_STATE_STOPPING&&running){pad({},false);check(xrEndSession(session),"xrEndSession");running=false;}
            if(sessionState!=XR_SESSION_STATE_FOCUSED){
                pad({},false);gestures.reset();
                bo2vr::updateGuestInput({},0);
                bo2vr::updateGuestTracking({},false,gameplay);
            }
        }
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
}
void makeReticle(){
    if(reticleSwap||reticleFailed)return;
    VkCommandBuffer cmd=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;bool acquired=false;
    try{
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
        sc.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        sc.format=xrFormat;sc.sampleCount=1;sc.width=sc.height=8;sc.faceCount=sc.arraySize=sc.mipCount=1;
        check(xrCreateSwapchain(session,&sc,&reticleSwap),"reticle swapchain");
        uint32_t count=0;check(xrEnumerateSwapchainImages(reticleSwap,0,&count,nullptr),"reticle image count");
        std::vector<XrSwapchainImageVulkanKHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        check(xrEnumerateSwapchainImages(reticleSwap,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"reticle images");
        uint32_t index;XrSwapchainImageAcquireInfo ac{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(xrAcquireSwapchainImage(reticleSwap,&ac,&index),"reticle acquire");acquired=true;
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;check(xrWaitSwapchainImage(reticleSwap,&wait),"reticle wait");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=commandPool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
        checkVk(VF(vkAllocateCommandBuffers)(device,&alloc,&cmd),"reticle command");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checkVk(VF(vkBeginCommandBuffer)(cmd,&begin),"reticle begin");
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=images[index].image;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VF(vkCmdPipelineBarrier)(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkClearColorValue color{{1,1,1,1}};VF(vkCmdClearColorImage)(cmd,b.image,b.newLayout,&color,1,&b.subresourceRange);
        b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        VF(vkCmdPipelineBarrier)(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
        checkVk(VF(vkEndCommandBuffer)(cmd),"reticle end");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checkVk(VF(vkCreateFence)(device,&fc,nullptr,&fence),"reticle fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;checkVk(VF(vkQueueSubmit)(gameQueue,1,&submit,fence),"reticle submit");
        checkVk(VF(vkWaitForFences)(device,1,&fence,VK_TRUE,UINT64_MAX),"reticle upload");
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(xrReleaseSwapchainImage(reticleSwap,&release),"reticle release");acquired=false;
        LOG("Controller aim reticle ready");
    }catch(const std::exception& e){
        LOG("Reticle unavailable: %s",e.what());reticleFailed=true;
        if(acquired){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(reticleSwap,&release);}
        if(reticleSwap)xrDestroySwapchain(reticleSwap);reticleSwap=XR_NULL_HANDLE;
    }
    if(fence)VF(vkDestroyFence)(device,fence,nullptr);
    if(cmd)VF(vkFreeCommandBuffers)(device,commandPool,1,&cmd);
}
VkResult hookedCreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* alloc,VkInstance* out){
    if(insideRuntime||!xrInstance)return realCreateInstance(info,alloc,out);
    std::lock_guard<std::recursive_mutex> hold(lock);RuntimeCall guard;
    try{
        XrVulkanInstanceCreateInfoKHR ci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};ci.systemId=xrSystem;ci.pfnGetInstanceProcAddr=realGipa;ci.vulkanCreateInfo=info;ci.vulkanAllocator=alloc;
        VkResult result=VK_ERROR_INITIALIZATION_FAILED;
        check(xrFunction<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR")(xrInstance,&ci,out,&result),"xrCreateVulkanInstanceKHR");
        if(result==VK_SUCCESS){vkInstance=*out;LOG("Game Vulkan instance uses OpenXR requirements");}return result;
    }catch(const std::exception& e){LOG("instance: %s",e.what());return VK_ERROR_INITIALIZATION_FAILED;}
}
VkResult hookedCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* alloc,VkDevice* out){
    if(insideRuntime||!xrInstance)return realCreateDevice(physical,info,alloc,out);
    std::lock_guard<std::recursive_mutex> hold(lock);RuntimeCall guard;
    try{
        XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
        check(xrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR")(xrInstance,xrSystem,&requirements),"Vulkan requirements");
        XrVulkanGraphicsDeviceGetInfoKHR gi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};gi.systemId=xrSystem;gi.vulkanInstance=vkInstance;
        VkPhysicalDevice expected;check(xrFunction<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR")(xrInstance,&gi,&expected),"OpenXR GPU");
        if(expected!=physical)throw std::runtime_error("game selected a different GPU from OpenXR");
        XrVulkanDeviceCreateInfoKHR ci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};ci.systemId=xrSystem;ci.pfnGetInstanceProcAddr=realGipa;ci.vulkanPhysicalDevice=physical;ci.vulkanCreateInfo=info;ci.vulkanAllocator=alloc;
        VkResult result=VK_ERROR_INITIALIZATION_FAILED;check(xrFunction<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR")(xrInstance,&ci,out,&result),"xrCreateVulkanDeviceKHR");
        if(result==VK_SUCCESS){devices[*out]={physical,info->pQueueCreateInfos[0].queueFamilyIndex};LOG("Game Vulkan device captured");}return result;
    }catch(const std::exception& e){LOG("device: %s",e.what());return VK_ERROR_INITIALIZATION_FAILED;}
}
VkResult hookedCreateSwapchain(VkDevice dev,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* alloc,VkSwapchainKHR* out){
    if(insideRuntime||!xrInstance)return realCreateSwapchain(dev,info,alloc,out);
    std::lock_guard<std::recursive_mutex> hold(lock);
    VkSwapchainCreateInfoKHR copy=*info;copy.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkResult result=realCreateSwapchain(dev,&copy,alloc,out);
    if(result!=VK_SUCCESS)return result;
    try{
        SwapInfo data{dev,info->imageExtent,info->imageFormat,{}};uint32_t count=0;
        auto get=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(realGdpa(dev,"vkGetSwapchainImagesKHR"));
        checkVk(get(dev,*out,&count,nullptr),"swap image count");data.images.resize(count);checkVk(get(dev,*out,&count,data.images.data()),"swap images");
        swaps[*out]=data;makeSession(dev,data);
    }catch(const std::exception& e){LOG("swapchain: %s",e.what());destroySession();}
    return result;
}
void barrier(VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=oldLayout;b.newLayout=newLayout;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VF(vkCmdPipelineBarrier)(copyCommand,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
VkResult hookedPresent(VkQueue queue,const VkPresentInfoKHR* present){
    if(insideRuntime||!session||present->swapchainCount!=1)return realPresent(queue,present);
    std::lock_guard<std::recursive_mutex> hold(lock);RuntimeCall guard;
    auto found=swaps.find(present->pSwapchains[0]);if(found==swaps.end()||queue!=gameQueue)return realPresent(queue,present);
    bool waitsConsumed=false;
    bool imageAcquired=false;
    XrTime displayTime=0;
    XrSwapchain captureSwap=xrSwap;
    try{
        bo2vr::pollRelayCommand();
        events();if(!running)return realPresent(queue,present);
        resizeCapture(found->second);
        auto now=Clock::now();
        if(previousFrame.time_since_epoch().count())frameTimes.push_back(std::chrono::duration<double,std::milli>(now-previousFrame).count());
        previousFrame=now;
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};check(xrWaitFrame(session,&wi,&fs),"xrWaitFrame");
        maximumWait=std::max(maximumWait,milliseconds(now));
        displayTime=fs.predictedDisplayTime;
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};check(xrBeginFrame(session,&bi),"xrBeginFrame");frameOpen=true;
        bo2vr::InputSample state;check(input.sample(localSpace,fs.predictedDisplayTime,sessionState==XR_SESSION_STATE_FOCUSED,state),"controller sample");
        std::array<XrView,2> eyeViews{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=displayTime;locate.space=localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};uint32_t viewCount=0;
        XrResult located=xrLocateViews(session,&locate,&viewState,2,&viewCount,eyeViews.data());
        constexpr auto requiredFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        bool valid=XR_SUCCEEDED(located)&&viewCount==2&&(viewState.viewStateFlags&requiredFlags)==requiredFlags&&state.focused;
        XrVector3f head{(eyeViews[0].pose.position.x+eyeViews[1].pose.position.x)*0.5f,(eyeViews[0].pose.position.y+eyeViews[1].pose.position.y)*0.5f,(eyeViews[0].pose.position.z+eyeViews[1].pose.position.z)*0.5f};
        unsigned punches=gestures.update(state,head,displayTime,valid&&gameplay);
        if(punches)LOG("Punch gesture hands=%u",punches);
        bo2vr::updateGuestInput(state,displayTime);
        auto mapped=bindings.map(state);
        if(gameplay&&bo2vr::guestControllerAimEnabled()){mapped.ry=0;if(!(state.hands[1].aimFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))mapped.rightTrigger=0;}
        pad(mapped,state.focused);
        bo2vr::updateGuestTracking(eyeViews,valid,gameplay);
        if(wasGameplay&&!gameplay)menuAnchored=false;
        wasGameplay=gameplay;
        if(!menuAnchored&&valid){
            auto q=eyeViews[0].pose.orientation;
            float yaw=std::atan2(2*(q.x*q.z+q.w*q.y),1-2*(q.x*q.x+q.y*q.y));
            menuPose.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
            menuPose.position={head.x-2.5f*std::sin(yaw),head.y,head.z-2.5f*std::cos(yaw)};menuAnchored=true;
        }
        unsigned renderedEye=0;
        bool stereo=bo2vr::guestStereoViews(eyeViews,renderedEye);
        if(stereo)makeReticle();
        if(stereo&&!sameFrameStereo)createEyeCaptures();
        else for(auto& eye:eyeCaptures)eye.ready=false;
        captureSwap=stereo&&!sameFrameStereo?eyeCaptures[renderedEye].swap:xrSwap;
        auto& captureImages=stereo&&!sameFrameStereo?eyeCaptures[renderedEye].images:xrImages;
        XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad reticle{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrPosef aimHead{},aimPose{};
        bool drawReticle=stereo&&reticleSwap&&bo2vr::guestAimPose(aimHead,aimPose);
        if(drawReticle){
            auto forward=bo2vr::rotate(aimPose.orientation,{0,0,-1});
            reticle.space=localSpace;reticle.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            reticle.pose.orientation=aimHead.orientation;
            reticle.pose.position={aimHead.position.x+forward.x*4,aimHead.position.y+forward.y*4,aimHead.position.z+forward.z*4};
            reticle.size={0.012f,0.012f};reticle.subImage.swapchain=reticleSwap;reticle.subImage.imageRect.extent={8,8};
        }
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::array<XrCompositionLayerProjectionView,2> projectionViews{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer),reinterpret_cast<XrCompositionLayerBaseHeader*>(&reticle)};
        if(fs.shouldRender){
            SourceCapture diagnostic;
            uint32_t index;XrSwapchainImageAcquireInfo ac{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(xrAcquireSwapchainImage(captureSwap,&ac,&index),"acquire image");
            imageAcquired=true;
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;check(xrWaitSwapchainImage(captureSwap,&wait),"wait image");
            auto copyWait=Clock::now();
            checkVk(VF(vkWaitForFences)(device,1,&copyFence,VK_TRUE,UINT64_MAX),"previous copy fence");
            maximumCopyWait=std::max(maximumCopyWait,milliseconds(copyWait));
            checkVk(VF(vkResetCommandBuffer)(copyCommand,0),"reset copy");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checkVk(VF(vkBeginCommandBuffer)(copyCommand,&begin),"begin copy");
            VkImage source=found->second.images.at(present->pImageIndices[0]);VkImage target=captureImages.at(index).image;
            barrier(source,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            diagnostic.record(source,found->second.format);
            barrier(target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
            if(found->second.extent.width!=xrWidth || found->second.extent.height!=xrHeight)throw std::runtime_error("game surface resized; restart required");
            VkImageCopy region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.dstSubresource=region.srcSubresource;region.extent={xrWidth,xrHeight,1};
            VF(vkCmdCopyImage)(copyCommand,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            barrier(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_READ_BIT,0);
            barrier(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
            checkVk(VF(vkEndCommandBuffer)(copyCommand),"end copy");
            std::vector<VkPipelineStageFlags> stages(present->waitSemaphoreCount,VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=present->waitSemaphoreCount;submit.pWaitSemaphores=present->pWaitSemaphores;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&copyCommand;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&copyDone;
            checkVk(VF(vkResetFences)(device,1,&copyFence),"reset copy fence");
            checkVk(VF(vkQueueSubmit)(queue,1,&submit,copyFence),"submit copy");waitsConsumed=true;
            diagnostic.pending=diagnostic.buffer!=VK_NULL_HANDLE;
            diagnostic.save(found->second.format);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(xrReleaseSwapchainImage(captureSwap,&release),"release image");
            imageAcquired=false;
            // Intermediate capture path: a 2D game panel, explicitly not stereo game rendering.
            layer.space=localSpace;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;layer.subImage.swapchain=xrSwap;layer.subImage.imageRect.extent={int32_t(xrWidth),int32_t(xrHeight)};layer.pose=menuPose;layer.size={3.2f,3.2f*xrHeight/xrWidth};
            if(stereo){
                if(sameFrameStereo){
                    for(int eye=0;eye<2;++eye){auto& pv=projectionViews[eye];pv.pose=eyeViews[eye].pose;pv.fov=eyeViews[eye].fov;pv.subImage.swapchain=xrSwap;pv.subImage.imageRect.offset={bo2vr::guestMonoscopic()?0:eye*int32_t(xrWidth/2),0};pv.subImage.imageRect.extent={int32_t(bo2vr::guestMonoscopic()?xrWidth:xrWidth/2),int32_t(xrHeight)};}
                }else{
                    eyeCaptures[renderedEye].ready=true;eyeCaptures[renderedEye].view=eyeViews[renderedEye];
                    for(int eye=0;eye<2;++eye){auto& pv=projectionViews[eye];pv.pose=eyeCaptures[eye].view.pose;pv.fov=eyeCaptures[eye].view.fov;pv.subImage.swapchain=eyeCaptures[eye].swap;pv.subImage.imageRect.extent={int32_t(xrWidth),int32_t(xrHeight)};}
                }
                projection.space=localSpace;projection.viewCount=2;projection.views=projectionViews.data();layers[0]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
            }
        }
        bool ready=fs.shouldRender&&(!stereo||sameFrameStereo||(eyeCaptures[0].ready&&eyeCaptures[1].ready));
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=ready?(drawReticle?2:1):0;end.layers=ready?layers:nullptr;
        check(xrEndFrame(session,&end),"xrEndFrame");frameOpen=false;
        if(++frames%300==1)LOG("XR frame %llu focused=%d LT=%.2f RT=%.2f buttons=%04x",frames,state.focused,state.hands[0].trigger,state.hands[1].trigger,bindings.map(state).buttons);
        if(frameTimes.size()>=300){std::sort(frameTimes.begin(),frameTimes.end());LOG("XR timing ms P50=%.1f P95=%.1f worst=%.1f maxWait=%.1f maxCopyFence=%.1f",frameTimes[150],frameTimes[285],frameTimes.back(),maximumWait,maximumCopyWait);frameTimes.clear();maximumWait=maximumCopyWait=0;}
    }catch(const std::exception& e){
        LOG("frame: %s",e.what());pad({},false);
        if(imageAcquired){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(captureSwap,&release);}
        if(frameOpen){XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=displayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&end);frameOpen=false;}
    }
    VkPresentInfoKHR next=*present;if(waitsConsumed){next.waitSemaphoreCount=1;next.pWaitSemaphores=&copyDone;}
    return realPresent(queue,&next);
}
void hookedDestroySwapchain(VkDevice dev,VkSwapchainKHR swap,const VkAllocationCallbacks* alloc){std::lock_guard<std::recursive_mutex> hold(lock);swaps.erase(swap);realDestroySwapchain(dev,swap,alloc);}
void hookedDestroyDevice(VkDevice dev,const VkAllocationCallbacks* alloc){std::lock_guard<std::recursive_mutex> hold(lock);if(dev==device)destroySession();devices.erase(dev);realDestroyDevice(dev,alloc);}
PFN_vkVoidFunction hookedGipa(VkInstance,const char*);
PFN_vkVoidFunction hookedGdpa(VkDevice,const char*);
PFN_vkVoidFunction replacement(const char* name){
    if(!strcmp(name,"vkGetInstanceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(hookedGipa);
    if(!strcmp(name,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(hookedGdpa);
    if(!strcmp(name,"vkCreateInstance"))return reinterpret_cast<PFN_vkVoidFunction>(hookedCreateInstance);
    if(!strcmp(name,"vkCreateDevice"))return reinterpret_cast<PFN_vkVoidFunction>(hookedCreateDevice);
    if(!strcmp(name,"vkCreateSwapchainKHR"))return reinterpret_cast<PFN_vkVoidFunction>(hookedCreateSwapchain);
    if(!strcmp(name,"vkQueuePresentKHR"))return reinterpret_cast<PFN_vkVoidFunction>(hookedPresent);
    if(!strcmp(name,"vkDestroyDevice"))return reinterpret_cast<PFN_vkVoidFunction>(hookedDestroyDevice);
    if(!strcmp(name,"vkDestroySwapchainKHR"))return reinterpret_cast<PFN_vkVoidFunction>(hookedDestroySwapchain);
    return nullptr;
}
PFN_vkVoidFunction hookedGipa(VkInstance instance,const char* name){if(!insideRuntime)if(auto f=replacement(name))return f;return realGipa(instance,name);}
PFN_vkVoidFunction hookedGdpa(VkDevice dev,const char* name){if(!insideRuntime)if(auto f=replacement(name))return f;return realGdpa(dev,name);}
template<class T>void install(void* lib,const char* name,void* hook,T& original){void* address=dlsym(lib,name);if(!address||DobbyHook(address,reinterpret_cast<dobby_dummy_func_t>(hook),reinterpret_cast<dobby_dummy_func_t*>(&original))!=0)throw std::runtime_error(name);LOG("Hooked %s",name);}
}

extern "C" JNIEXPORT void JNICALL Java_com_bo2vr_quest_QuestBridge_nativeStart(JNIEnv* env,jclass,jobject app,jstring directory){
    std::lock_guard<std::recursive_mutex> hold(lock);
    if(xrInstance)return;
    env->GetJavaVM(&vm);activity=env->NewGlobalRef(app);
    const char* path=env->GetStringUTFChars(directory,nullptr);files=path;env->ReleaseStringUTFChars(directory,path);
    try{
        RuntimeCall guard;
        PFN_xrInitializeLoaderKHR initialize=nullptr;
        check(xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&initialize)),"loader function");
        XrLoaderInitInfoAndroidKHR init{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};init.applicationVM=vm;init.applicationContext=activity;
        check(initialize(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&init)),"loader init");
        std::vector<const char*> extensions={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
        uint32_t extensionCount=0;check(xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr),"extension count");
        std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
        check(xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,available.data()),"extensions");
        for(const auto& extension:available)if(!strcmp(extension.extensionName,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
        XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};android.applicationVM=vm;android.applicationActivity=activity;
        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};create.next=&android;strcpy(create.applicationInfo.applicationName,"BO2 Quest VR Development");create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,34);create.enabledExtensionCount=extensions.size();create.enabledExtensionNames=extensions.data();
        check(xrCreateInstance(&create,&xrInstance),"xrCreateInstance");
        if(extensions.size()>2)xrGetInstanceProcAddr(xrInstance,"xrPerfSettingsSetPerformanceLevelEXT",reinterpret_cast<PFN_xrVoidFunction*>(&setPerformance));
        XrSystemGetInfo system{XR_TYPE_SYSTEM_GET_INFO};system.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;check(xrGetSystem(xrInstance,&system,&xrSystem),"xrGetSystem");
        void* vulkan=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);if(!vulkan)throw std::runtime_error("libvulkan.so");
        install(vulkan,"vkCreateInstance",reinterpret_cast<void*>(hookedCreateInstance),realCreateInstance);
        install(vulkan,"vkCreateDevice",reinterpret_cast<void*>(hookedCreateDevice),realCreateDevice);
        install(vulkan,"vkCreateSwapchainKHR",reinterpret_cast<void*>(hookedCreateSwapchain),realCreateSwapchain);
        install(vulkan,"vkQueuePresentKHR",reinterpret_cast<void*>(hookedPresent),realPresent);
        install(vulkan,"vkDestroyDevice",reinterpret_cast<void*>(hookedDestroyDevice),realDestroyDevice);
        install(vulkan,"vkDestroySwapchainKHR",reinterpret_cast<void*>(hookedDestroySwapchain),realDestroySwapchain);
        install(vulkan,"vkGetInstanceProcAddr",reinterpret_cast<void*>(hookedGipa),realGipa);
        install(vulkan,"vkGetDeviceProcAddr",reinterpret_cast<void*>(hookedGdpa),realGdpa);
        LOG("OpenXR initialized; awaiting the game Vulkan swapchain");
    }catch(const std::exception& e){LOG("VR INITIALIZATION FAILED: %s",e.what());env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),e.what());}
}
