#include "controller_bindings.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace bo2vr {
const std::array<const char*, ControllerBindings::Count> ControllerBindings::keys = {
    "left_trigger", "right_trigger", "left_grip", "right_grip", "x", "y", "a", "b", "left_stick", "right_stick", "menu", "left_punch", "right_punch"
};
static const char* names[] = {"none","fire","aim","jump","crouch","reload_use","weapon","sprint","melee","grenade","tactical","pause","scores"};
ControllerBindings::ControllerBindings() : actions{GameAction::Aim, GameAction::Fire, GameAction::Tactical, GameAction::Grenade,
    GameAction::ReloadUse, GameAction::Weapon, GameAction::Jump, GameAction::Crouch, GameAction::Sprint, GameAction::Melee, GameAction::Pause, GameAction::Melee, GameAction::Melee} {}
bool ControllerBindings::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    ControllerBindings next;
    std::string line;
    while (std::getline(file,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto split = line.find('=');
        if (split == std::string::npos) continue;
        auto key = line.substr(0,split), value = line.substr(split+1);
        for (size_t i=0;i<Count;++i) if (key == keys[i]) {
            for (size_t a=0;a<std::size(names);++a) if (value == names[a]) next.actions[i] = static_cast<GameAction>(a);
        }
        if (key == "swap_sticks") next.swapSticks = value == "true";
        if (key == "deadzone") {
            char* end = nullptr;
            float parsed = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && *end == 0 && std::isfinite(parsed)) next.deadzone = std::clamp(parsed,0.05f,0.5f);
        }
    }
    *this = next;
    return true;
}
GamepadOutput ControllerBindings::map(const InputSample& sample) const {
    GamepadOutput out;
    if (!sample.focused) return out;
    const auto& l = sample.hands[0]; const auto& r = sample.hands[1];
    const float values[] = {l.trigger,r.trigger,l.squeeze,r.squeeze,float(l.primary),float(l.secondary),float(r.primary),float(r.secondary),float(l.stickClick),float(r.stickClick),float(l.menu),float(l.punch),float(r.punch)};
    const uint16_t bits[] = {0,0,0,4096,8192,16384,32768,64,128,512,256,16,32};
    for (size_t i=0;i<Count;++i) {
        const float value = std::isfinite(values[i]) ? std::clamp(values[i],0.0f,1.0f) : 0;
        if (actions[i] == GameAction::Fire) out.rightTrigger = std::max(out.rightTrigger,static_cast<uint8_t>(value*255));
        else if (actions[i] == GameAction::Aim) out.leftTrigger = std::max(out.leftTrigger,static_cast<uint8_t>(value*255));
        else if (value >= 0.55f) out.buttons |= bits[static_cast<unsigned>(actions[i])];
    }
    auto stick = [&](XrVector2f xy, int16_t& x, int16_t& y) {
        if (!std::isfinite(xy.x) || !std::isfinite(xy.y)) return;
        const float length = std::hypot(xy.x,xy.y);
        if (length <= deadzone) return;
        const float scale = (std::min(length,1.0f)-deadzone)/(1.0f-deadzone)/length;
        x = static_cast<int16_t>(std::clamp(xy.x*scale,-1.0f,1.0f)*32767);
        y = static_cast<int16_t>(std::clamp(xy.y*scale,-1.0f,1.0f)*32767);
    };
    stick(swapSticks ? r.stick : l.stick,out.lx,out.ly);
    stick(swapSticks ? l.stick : r.stick,out.rx,out.ry);
    return out;
}
}
