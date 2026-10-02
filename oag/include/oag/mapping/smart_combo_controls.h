#pragma once
#include "oag/config/smart_combo_config.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"
namespace oag {
struct OagSmartInput {
    LogicalGamepadState pad {};
    KeyboardState keyboard {};
    MouseState mouse {};
};
struct OagSmartOutput {
    LogicalGamepadState pad {};
    KeyboardState keyboard {};
    MouseState mouse {};
    std::uint8_t axes = 0; // left/right stick + L2/R2 ownership, including zero
};
bool oagSmartDown(const OagSmartTarget&, const OagSmartInput&, std::uint16_t threshold = 500);
void oagSmartSet(const OagSmartTarget&, bool down, OagSmartOutput&);
void oagSmartMerge(const OagSmartOutput&, OagSmartOutput&);
void oagSmartCompose(const OagSmartOutput&, LogicalGamepadState&);
void oagSmartConsume(const OagSmartTarget&, OagSmartInput&);
bool oagSmartSame(const OagSmartTarget&, const OagSmartTarget&);
} // namespace oag
