#pragma once
#include <openxr/openxr.h>
#include <array>

namespace bo2vr {
struct HandSample {
    XrPosef aim{{0, 0, 0, 1}, {0, 0, 0}};
    XrPosef grip{{0, 0, 0, 1}, {0, 0, 0}};
    XrSpaceLocationFlags aimFlags = 0, gripFlags = 0;
    XrVector2f stick{};
    float trigger = 0, squeeze = 0;
    bool primary = false, secondary = false, stickClick = false, menu = false;
    bool punch = false;
};
struct InputSample {
    std::array<HandSample, 2> hands{};
    bool focused = false;
};

// Owns actions/spaces, but not the renderer's instance or session.
// Initialize before any action sets are attached to that session. The owner
// must destroy this object before destroying its session and instance.
class QuestInput {
public:
    QuestInput() = default;
    ~QuestInput();
    QuestInput(const QuestInput&) = delete;
    QuestInput& operator=(const QuestInput&) = delete;
    XrResult initialize(XrInstance instance, XrSession session);
    XrResult sample(XrSpace baseSpace, XrTime displayTime,
                    bool focused, InputSample& result);
    XrResult haptic(unsigned hand, float amplitude, XrDuration duration);
    void shutdown();
private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet set_ = XR_NULL_HANDLE;
    std::array<XrPath, 2> paths_{};
    std::array<XrSpace, 2> aimSpaces_{}, gripSpaces_{};
    XrAction aim_ = XR_NULL_HANDLE, grip_ = XR_NULL_HANDLE;
    XrAction trigger_ = XR_NULL_HANDLE, squeeze_ = XR_NULL_HANDLE;
    XrAction stick_ = XR_NULL_HANDLE, primary_ = XR_NULL_HANDLE;
    XrAction secondary_ = XR_NULL_HANDLE, stickClick_ = XR_NULL_HANDLE;
    XrAction haptic_ = XR_NULL_HANDLE;
    XrAction menu_ = XR_NULL_HANDLE;
    bool attached_ = false;
};
} // namespace bo2vr
