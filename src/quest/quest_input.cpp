#include "quest_input.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace bo2vr {
QuestInput::~QuestInput() { shutdown(); }

void QuestInput::shutdown() {
    for (auto& space : aimSpaces_) {
        if (space) xrDestroySpace(space);
        space = XR_NULL_HANDLE;
    }
    for (auto& space : gripSpaces_) {
        if (space) xrDestroySpace(space);
        space = XR_NULL_HANDLE;
    }
    if (set_) xrDestroyActionSet(set_);
    set_ = XR_NULL_HANDLE;
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    attached_ = false;
}

XrResult QuestInput::initialize(XrInstance instance, XrSession session) {
    if (set_ || !instance || !session) return XR_ERROR_CALL_ORDER_INVALID;
    instance_ = instance;
    session_ = session;
    auto fail = [&](XrResult error) { shutdown(); return error; };
    XrResult r = xrStringToPath(instance, "/user/hand/left", &paths_[0]);
    if (XR_FAILED(r)) return fail(r);
    r = xrStringToPath(instance, "/user/hand/right", &paths_[1]);
    if (XR_FAILED(r)) return fail(r);
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "bo2_gameplay");
    std::snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName), "BO2 Gameplay");
    r = xrCreateActionSet(instance, &setInfo, &set_);
    if (XR_FAILED(r)) return fail(r);
    auto action = [&](const char* name, XrActionType type, XrAction& out) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
        std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", name);
        info.actionType = type;
        info.countSubactionPaths = 2;
        info.subactionPaths = paths_.data();
        return xrCreateAction(set_, &info, &out);
    };
    struct ActionSpec { const char* name; XrActionType type; XrAction* action; };
    const ActionSpec specs[] = {
        {"aim", XR_ACTION_TYPE_POSE_INPUT, &aim_},
        {"grip", XR_ACTION_TYPE_POSE_INPUT, &grip_},
        {"trigger", XR_ACTION_TYPE_FLOAT_INPUT, &trigger_},
        {"squeeze", XR_ACTION_TYPE_FLOAT_INPUT, &squeeze_},
        {"stick", XR_ACTION_TYPE_VECTOR2F_INPUT, &stick_},
        {"primary", XR_ACTION_TYPE_BOOLEAN_INPUT, &primary_},
        {"secondary", XR_ACTION_TYPE_BOOLEAN_INPUT, &secondary_},
        {"stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, &stickClick_},
        {"haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, &haptic_},
        {"menu", XR_ACTION_TYPE_BOOLEAN_INPUT, &menu_},
    };
    for (const auto& spec : specs) {
        r = action(spec.name, spec.type, *spec.action);
        if (XR_FAILED(r)) return fail(r);
    }
    std::vector<XrActionSuggestedBinding> bindings;
    auto bind = [&](XrAction a, const std::string& path) {
        XrPath p = XR_NULL_PATH;
        XrResult result = xrStringToPath(instance, path.c_str(), &p);
        if (XR_SUCCEEDED(result)) bindings.push_back({a, p});
        return result;
    };
    for (unsigned hand = 0; hand < 2; ++hand) {
        const std::string root = hand == 0 ? "/user/hand/left" : "/user/hand/right";
        const std::pair<XrAction, const char*> common[] = {
            {aim_, "/input/aim/pose"}, {grip_, "/input/grip/pose"},
            {trigger_, "/input/trigger/value"}, {squeeze_, "/input/squeeze/value"},
            {stick_, "/input/thumbstick"}, {stickClick_, "/input/thumbstick/click"},
            {haptic_, "/output/haptic"},
            {primary_, hand == 0 ? "/input/x/click" : "/input/a/click"},
            {secondary_, hand == 0 ? "/input/y/click" : "/input/b/click"},
        };
        for (const auto& b : common) {
            r = bind(b.first, root + b.second);
            if (XR_FAILED(r)) return fail(r);
        }
    }
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    r = bind(menu_, "/user/hand/left/input/menu/click");
    if (XR_FAILED(r)) return fail(r);
    r = xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &suggested.interactionProfile);
    if (XR_FAILED(r)) return fail(r);
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    r = xrSuggestInteractionProfileBindings(instance, &suggested);
    if (XR_FAILED(r)) return fail(r);
    for (unsigned hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.subactionPath = paths_[hand];
        info.poseInActionSpace.orientation.w = 1;
        info.action = aim_;
        r = xrCreateActionSpace(session, &info, &aimSpaces_[hand]);
        if (XR_FAILED(r)) return fail(r);
        info.action = grip_;
        r = xrCreateActionSpace(session, &info, &gripSpaces_[hand]);
        if (XR_FAILED(r)) return fail(r);
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &set_;
    r = xrAttachSessionActionSets(session, &attach);
    if (XR_FAILED(r)) return fail(r);
    attached_ = true;
    return XR_SUCCESS;
}

XrResult QuestInput::sample(XrSpace base, XrTime time, bool focused, InputSample& out) {
    out = {}; // Never leave firing or movement latched after focus/tracking loss.
    if (!attached_) return XR_ERROR_CALL_ORDER_INVALID;
    if (!focused) return XR_SUCCESS;
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    XrResult r = xrSyncActions(session_, &sync);
    if (r == XR_SESSION_NOT_FOCUSED) return XR_SUCCESS;
    if (XR_FAILED(r)) return r;
    InputSample current{};
    for (unsigned hand = 0; hand < 2; ++hand) {
        auto& result = current.hands[hand];
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = paths_[hand];
        auto scalar = [&](XrAction a, float& value) {
            get.action = a;
            XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            XrResult status = xrGetActionStateFloat(session_, &get, &state);
            if (XR_SUCCEEDED(status) && state.isActive) value = state.currentState;
            return status;
        };
        auto boolean = [&](XrAction a, bool& value) {
            get.action = a;
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            XrResult status = xrGetActionStateBoolean(session_, &get, &state);
            if (XR_SUCCEEDED(status) && state.isActive) value = state.currentState;
            return status;
        };
        auto pose = [&](XrAction a, XrSpace space, XrPosef& value, XrSpaceLocationFlags& flags) {
            get.action = a;
            XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
            XrResult status = xrGetActionStatePose(session_, &get, &state);
            if (XR_FAILED(status) || !state.isActive) return status;
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            status = xrLocateSpace(space, base, time, &location);
            if (XR_SUCCEEDED(status)) {
                flags = location.locationFlags;
                value = location.pose;
            }
            return status;
        };
        if (XR_FAILED(r = scalar(trigger_, result.trigger))) return r;
        if (XR_FAILED(r = scalar(squeeze_, result.squeeze))) return r;
        if (XR_FAILED(r = boolean(primary_, result.primary))) return r;
        if (XR_FAILED(r = boolean(secondary_, result.secondary))) return r;
        if (XR_FAILED(r = boolean(stickClick_, result.stickClick))) return r;
        if (hand == 0 && XR_FAILED(r = boolean(menu_, result.menu))) return r;
        get.action = stick_;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_FAILED(r = xrGetActionStateVector2f(session_, &get, &stick))) return r;
        if (stick.isActive) result.stick = stick.currentState;
        if (XR_FAILED(r = pose(aim_, aimSpaces_[hand], result.aim, result.aimFlags))) return r;
        if (XR_FAILED(r = pose(grip_, gripSpaces_[hand], result.grip, result.gripFlags))) return r;
    }
    current.focused = true;
    out = current;
    return XR_SUCCESS;
}

XrResult QuestInput::haptic(unsigned hand, float amplitude, XrDuration duration) {
    if (!attached_) return XR_ERROR_CALL_ORDER_INVALID;
    if (hand >= 2 || !std::isfinite(amplitude) || (duration <= 0 && duration != XR_MIN_HAPTIC_DURATION))
        return XR_ERROR_VALIDATION_FAILURE;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = haptic_;
    info.subactionPath = paths_[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
    vibration.duration = duration;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    return xrApplyHapticFeedback(session_, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}
} // namespace bo2vr
