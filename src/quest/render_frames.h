#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cstdint>
#include <map>
namespace bo2vr {
struct RenderFrame {
    std::array<XrView,2> views{};
    std::array<uint32_t,2> indices{};
    unsigned eyeCount=0;
};
// Engine buffers can arrive later than the newest frontend pose. Consume by
// exact buffer identity, never by whichever pose was most recently published.
class RenderFrames {
    std::map<uint32_t,RenderFrame> pending;
public:
    void publish(uint32_t buffer,const RenderFrame& frame){
        if(!buffer)return;
        if(pending.size()>=8&&!pending.count(buffer))pending.clear();
        pending[buffer]=frame;
    }
    RenderFrame take(uint32_t buffer){
        auto i=pending.find(buffer);if(i==pending.end())return {};
        auto frame=i->second;pending.erase(i);return frame;
    }
};
}
