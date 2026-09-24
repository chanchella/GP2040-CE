#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"

namespace oag {

KeyboardMouseGamepadMapper::KeyboardMouseGamepadMapper() {
    loadDefaultFpsProfile();
}

void KeyboardMouseGamepadMapper::loadDefaultFpsProfile() {
    bindings_.clear();

    // Movement: WASD -> left analog stick.
    bindings_.addBinding(
        keyboardUsage(0x1A), // W
        LogicalDigitalControl::LeftStickUp
    );
    bindings_.addBinding(
        keyboardUsage(0x16), // S
        LogicalDigitalControl::LeftStickDown
    );
    bindings_.addBinding(
        keyboardUsage(0x04), // A
        LogicalDigitalControl::LeftStickLeft
    );
    bindings_.addBinding(
        keyboardUsage(0x07), // D
        LogicalDigitalControl::LeftStickRight
    );

    // Common face-button cluster.
    bindings_.addBinding(
        keyboardUsage(0x2C), // Space
        LogicalDigitalControl::South
    );
    bindings_.addBinding(
        keyboardUsage(0x08), // E
        LogicalDigitalControl::East
    );
    bindings_.addBinding(
        keyboardUsage(0x14), // Q
        LogicalDigitalControl::West
    );
    bindings_.addBinding(
        keyboardUsage(0x15), // R
        LogicalDigitalControl::North
    );

    // Navigation / utility.
    bindings_.addBinding(
        keyboardUsage(0x2B), // Tab
        LogicalDigitalControl::Back
    );
    bindings_.addBinding(
        keyboardUsage(0x28), // Enter
        LogicalDigitalControl::Start
    );
    bindings_.addBinding(
        keyboardUsage(0x06), // C
        LogicalDigitalControl::LeftStickClick
    );
    bindings_.addBinding(
        keyboardUsage(0x09), // F
        LogicalDigitalControl::RightBumper
    );

    // Arrow keys -> D-pad.
    bindings_.addBinding(
        keyboardUsage(0x52),
        LogicalDigitalControl::DpadUp
    );
    bindings_.addBinding(
        keyboardUsage(0x51),
        LogicalDigitalControl::DpadDown
    );
    bindings_.addBinding(
        keyboardUsage(0x50),
        LogicalDigitalControl::DpadLeft
    );
    bindings_.addBinding(
        keyboardUsage(0x4F),
        LogicalDigitalControl::DpadRight
    );

    // Mouse buttons.
    bindings_.addBinding(
        mouseButton(MouseButtonLeft),
        LogicalDigitalControl::RightTrigger
    );
    bindings_.addBinding(
        mouseButton(MouseButtonRight),
        LogicalDigitalControl::LeftTrigger
    );
    bindings_.addBinding(
        mouseButton(MouseButtonMiddle),
        LogicalDigitalControl::RightStickClick
    );
    bindings_.addBinding(
        mouseButton(MouseButtonBack),
        LogicalDigitalControl::LeftBumper
    );
    bindings_.addBinding(
        mouseButton(MouseButtonForward),
        LogicalDigitalControl::RightBumper
    );

    // Conservative default aim curve. This is intentionally configurable.
    mouseConfig_.sensitivityX = 0.018;
    mouseConfig_.sensitivityY = 0.018;
    mouseConfig_.exponent = 1.35;
    mouseConfig_.deadzoneX = 0.08;
    mouseConfig_.deadzoneY = 0.08;
    mouseConfig_.boundary = StickBoundary::Circle;
    mouseConfig_.invertY = false;
}

LogicalGamepadState KeyboardMouseGamepadMapper::apply(
    const KeyboardState* keyboard,
    const MouseState* mouse,
    MouseMotion mouseMotion,
    LogicalGamepadState base
) const {
    LogicalGamepadState output =
        bindings_.apply(keyboard, mouse, base);

    const StickVector aim =
        mouseMapper_.map(mouseMotion, mouseConfig_);

    if (mouseMotion.dx != 0 || mouseMotion.dy != 0) {
        output.rx = aim.x;
        output.ry = aim.y;
    }

    const bool kmConnected =
        (keyboard != nullptr && keyboard->connected) ||
        (mouse != nullptr && mouse->connected);

    if (kmConnected) {
        output.connected = true;
    }

    return output;
}

} // namespace oag
