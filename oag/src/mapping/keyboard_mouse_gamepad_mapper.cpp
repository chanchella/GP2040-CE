#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"

namespace oag {

KeyboardMouseGamepadMapper::KeyboardMouseGamepadMapper() {
    loadDefaultFpsProfile();
}

void KeyboardMouseGamepadMapper::loadDefaultFpsProfile() {
    bindings_.clear();
    extraBindings_.clear();
    extraBindSlots_ = {};

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

    // Core mouse buttons stay fixed in the FPS profile.
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

    // Six isolated extra bind slots:
    //   0..3 = keyboard F1..F4
    //   4..5 = mouse Back / Forward side buttons.
    // Defaults are intentionally useful but every slot can be reassigned with
    // configureExtraBind() without touching the base FPS mapping.
    extraBindSlots_[0] = {
        true,
        keyboardUsage(0x3A), // F1
        LogicalDigitalControl::Guide,
    };
    extraBindSlots_[1] = {
        true,
        keyboardUsage(0x3B), // F2
        LogicalDigitalControl::Back,
    };
    extraBindSlots_[2] = {
        true,
        keyboardUsage(0x3C), // F3
        LogicalDigitalControl::Start,
    };
    extraBindSlots_[3] = {
        true,
        keyboardUsage(0x3D), // F4
        LogicalDigitalControl::LeftStickClick,
    };
    extraBindSlots_[4] = {
        true,
        mouseButton(MouseButtonBack),
        LogicalDigitalControl::LeftBumper,
    };
    extraBindSlots_[5] = {
        true,
        mouseButton(MouseButtonForward),
        LogicalDigitalControl::RightBumper,
    };

    rebuildExtraBindings();

    // Ultra-responsive mouse->right-stick curve. The first mouse count
    // deliberately lands around one-third stick travel so game deadzones are
    // cleared immediately, while the sub-linear response keeps medium motion
    // progressive and fast flicks approach full analog deflection.
    mouseConfig_.sensitivityX = 0.045;
    mouseConfig_.sensitivityY = 0.045;
    mouseConfig_.exponent = 0.58;
    mouseConfig_.deadzoneX = 0.18;
    mouseConfig_.deadzoneY = 0.18;
    mouseConfig_.boundary = StickBoundary::Circle;
    mouseConfig_.invertY = false;
}

bool KeyboardMouseGamepadMapper::configureExtraBind(
    std::size_t slot,
    BindingSource source,
    LogicalDigitalControl target
) {
    if (slot >= extraBindSlots_.size()) {
        return false;
    }

    const ExtraBindSlot previous = extraBindSlots_[slot];

    extraBindSlots_[slot] = {
        true,
        source,
        target,
    };

    if (rebuildExtraBindings()) {
        return true;
    }

    extraBindSlots_[slot] = previous;
    rebuildExtraBindings();
    return false;
}

bool KeyboardMouseGamepadMapper::disableExtraBind(
    std::size_t slot
) {
    if (slot >= extraBindSlots_.size()) {
        return false;
    }

    const ExtraBindSlot previous = extraBindSlots_[slot];
    extraBindSlots_[slot] = {};

    if (rebuildExtraBindings()) {
        return true;
    }

    extraBindSlots_[slot] = previous;
    rebuildExtraBindings();
    return false;
}

const ExtraBindSlot* KeyboardMouseGamepadMapper::extraBind(
    std::size_t slot
) const {
    if (slot >= extraBindSlots_.size()) {
        return nullptr;
    }

    return &extraBindSlots_[slot];
}

bool KeyboardMouseGamepadMapper::rebuildExtraBindings() {
    extraBindings_.clear();

    for (const ExtraBindSlot& slot : extraBindSlots_) {
        if (!slot.enabled) {
            continue;
        }

        if (!extraBindings_.addBinding(
                slot.source,
                slot.target
            )) {
            return false;
        }
    }

    return true;
}

LogicalGamepadState KeyboardMouseGamepadMapper::apply(
    const KeyboardState* keyboard,
    const MouseState* mouse,
    MouseMotion mouseMotion,
    LogicalGamepadState base
) const {
    LogicalGamepadState output =
        bindings_.apply(keyboard, mouse, base);

    output = extraBindings_.apply(
        keyboard,
        mouse,
        output
    );

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
