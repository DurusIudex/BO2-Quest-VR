#pragma once
#include "quest_input.h"
#include <array>
#include <cstdint>
#include <string>

namespace bo2vr {
enum class GameAction { None, Fire, Aim, Jump, Crouch, ReloadUse, Weapon, Sprint, Melee, Grenade, Tactical, Pause, Scores };
struct GamepadOutput { uint16_t buttons = 0; uint8_t leftTrigger = 0, rightTrigger = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0; };
struct ControllerBindings {
    static constexpr size_t Count = 13;
    static const std::array<const char*, Count> keys;
    std::array<GameAction, Count> actions;
    float deadzone = 0.15f;
    bool swapSticks = false;
    ControllerBindings();
    bool load(const std::string& path);
    GamepadOutput map(const InputSample&) const;
};
}
