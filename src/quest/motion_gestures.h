#pragma once
#include "quest_input.h"
namespace bo2vr {
// Detect a forward punch relative to the head so walking does not count.
// Invalid tracking/focus resets history; a held stroke cannot repeatedly fire.
class MotionGestures {
public:
    unsigned update(InputSample&,XrVector3f head,XrTime time,bool gameplay);
    void reset(){hands_={};}
private:
    struct State {XrVector3f position{};XrTime time=0,lastPunch=0,pulseUntil=0;bool armed=true;};
    std::array<State,2> hands_{};
};
}
