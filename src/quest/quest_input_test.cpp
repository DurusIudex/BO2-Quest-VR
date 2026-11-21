// Simulates runtime focus loss, inactive tracking, and failures without a headset.
#include "quest_input.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>

namespace {
uintptr_t nextHandle = 1;
template<class T> T handle() { return reinterpret_cast<T>(nextHandle++); }
std::map<std::string, XrAction> actions;
XrResult syncResult = XR_SUCCESS;
XrResult floatResult = XR_SUCCESS;
bool poseActive = true, failCreate = false;
int syncCalls = 0, locateCalls = 0, liveSpaces = 0, liveSets = 0;
float hapticAmplitude = 0;
}
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char*, XrPath* out) { *out = nextHandle++; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* out) { *out = handle<XrActionSet>(); ++liveSets; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet) { --liveSets; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo* info, XrAction* out) {
    if (failCreate) return XR_ERROR_RUNTIME_FAILURE;
    *out = handle<XrAction>(); actions[info->actionName] = *out; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* info) { assert(info->countSuggestedBindings == 19); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    assert(info->poseInActionSpace.orientation.w == 1); *out = handle<XrSpace>(); ++liveSpaces; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { --liveSpaces; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo*) { ++syncCalls; return syncResult; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo*, XrActionStateFloat* out) { out->isActive = true; out->currentState = 0.8f; return floatResult; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo*, XrActionStateBoolean* out) { out->isActive = true; out->currentState = true; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* out) { out->isActive = true; out->currentState = {0.2f, 0.9f}; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* out) { out->isActive = poseActive; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace, XrSpace, XrTime time, XrSpaceLocation* out) {
    assert(time == 1234); ++locateCalls;
    out->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation.w = 1; out->pose.position = {1, 2, 3}; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader* header) {
    hapticAmplitude = reinterpret_cast<const XrHapticVibration*>(header)->amplitude; return XR_SUCCESS;
}
}
int main() {
    const auto instance = handle<XrInstance>();
    const auto session = handle<XrSession>();
    const auto base = handle<XrSpace>();
    bo2vr::InputSample sample;
    {
        bo2vr::QuestInput input;
        assert(input.initialize(instance, session) == XR_SUCCESS);
        assert(liveSpaces == 4 && liveSets == 1);
        assert(input.sample(base, 1234, true, sample) == XR_SUCCESS);
        assert(sample.focused && sample.hands[1].trigger == 0.8f);
        assert(sample.hands[0].aim.position.z == 3 && locateCalls == 4);
        const int oldSync = syncCalls;
        assert(input.sample(base, 1234, false, sample) == XR_SUCCESS);
        assert(!sample.focused && sample.hands[1].trigger == 0 && syncCalls == oldSync);
        syncResult = XR_SESSION_NOT_FOCUSED;
        assert(input.sample(base, 1234, true, sample) == XR_SUCCESS);
        assert(!sample.focused && sample.hands[0].stick.y == 0);
        syncResult = XR_SUCCESS;
        floatResult = XR_ERROR_RUNTIME_FAILURE;
        assert(input.sample(base, 1234, true, sample) == XR_ERROR_RUNTIME_FAILURE);
        assert(!sample.focused && !sample.hands[0].primary);
        floatResult = XR_SUCCESS;
        poseActive = false;
        const int oldLocate = locateCalls;
        assert(input.sample(base, 1234, true, sample) == XR_SUCCESS);
        assert(!sample.hands[0].aimFlags && locateCalls == oldLocate);
        assert(input.haptic(0, 2, 1000) == XR_SUCCESS && hapticAmplitude == 1);
        assert(input.haptic(2, 1, 1000) == XR_ERROR_VALIDATION_FAILURE);
        assert(input.haptic(0, std::numeric_limits<float>::quiet_NaN(), 1000) == XR_ERROR_VALIDATION_FAILURE);
    }
    assert(liveSets == 0 && liveSpaces == 0);
    {
        bo2vr::QuestInput failed;
        failCreate = true;
        assert(failed.initialize(instance, session) == XR_ERROR_RUNTIME_FAILURE);
        assert(liveSets == 0 && liveSpaces == 0);
    }
    std::puts("PASS: focus loss, input reset, inactive tracking, haptic bounds, cleanup.");
}
