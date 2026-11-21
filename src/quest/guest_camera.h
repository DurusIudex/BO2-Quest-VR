#pragma once
#include <openxr/openxr.h>
#include <array>
#include <string>
#include "quest_input.h"
namespace bo2vr {
bool guestMonoscopic();
bool guestControllerAimEnabled();
bool guestIntermission();
const char* guestGameMode();
void installGuestCamera(const std::string& files);
void updateGuestTracking(const std::array<XrView,2>& views,bool valid,bool gameplay);
void updateGuestInput(const InputSample& input,XrTime time);
bool guestAimPose(XrPosef& head,XrPosef& aim);
bool guestStereoViews(std::array<XrView,2>& views,unsigned& eye);
}
