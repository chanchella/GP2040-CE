#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"

#include "oag/input/gamepad_state.h"

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

    // V8 custom controller layout. PlayStation-style names are mapped onto
    // the existing XInput logical controls: X/South, Circle/East,
    // Square/West, Triangle/North, L1/LB, R1/RB.
    bindings_.addBinding(
        keyboardUsage(0x2C), // Space -> X / South
        LogicalDigitalControl::South
    );
    bindings_.addBinding(
        keyboardUsage(0x06), // C -> Circle / East
        LogicalDigitalControl::East
    );
    bindings_.addBinding(
        keyboardUsage(0x09), // F -> Square / West
        LogicalDigitalControl::West
    );
    bindings_.addBinding(
        keyboardUsage(0x08), // E -> Triangle / North
        LogicalDigitalControl::North
    );
    bindings_.addBinding(
        keyboardUsage(0x14), // Q -> L1 / Left Bumper
        LogicalDigitalControl::LeftBumper
    );

    // Navigation / utility.
    bindings_.addBinding(
        keyboardUsage(0x29), // Escape -> Start
        LogicalDigitalControl::Start
    );
    bindings_.addBinding(
        keyboardUsage(0x10), // M -> Share / Back
        LogicalDigitalControl::Back
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
    //   0..3 = keyboard 1..4
    //   4..5 = mouse Back / Forward side buttons.
    // Defaults are intentionally useful but every slot can be reassigned with
    // configureExtraBind() without touching the base FPS mapping.
    extraBindSlots_[0] = {
        true,
        keyboardUsage(0x1E), // 1
        LogicalDigitalControl::Guide,
    };
    extraBindSlots_[1] = {
        true,
        keyboardUsage(0x1F), // 2
        LogicalDigitalControl::Back,
    };
    extraBindSlots_[2] = {
        true,
        keyboardUsage(0x20), // 3 -> Guide / Home
        LogicalDigitalControl::Guide,
    };
    extraBindSlots_[3] = {
        true,
        keyboardUsage(0x21), // 4
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

    // Legendary Aim V1: velocity-based precision-to-flick response.
    // One normalized count/ms stays above a typical controller deadzone,
    // while fast mouse motion ramps progressively to full stick travel.
    // The firmware normalizes report deltas to a 1 ms reference interval,
    // so 125/250/500/1000 Hz mice retain comparable physical sensitivity.
    mouseConfig_.sensitivityX = 1.4352;
    mouseConfig_.sensitivityY = 1.4352;
    mouseConfig_.exponent = 0.72;
    mouseConfig_.deadzoneX = 0.14;
    mouseConfig_.deadzoneY = 0.14;
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
    LogicalGamepadState base,
    double mouseMotionScale
) const {
    LogicalGamepadState output =
        bindings_.apply(keyboard, mouse, base);

    // HID keyboard modifiers are carried separately from normal key usages.
    // Treat either Shift key as R1 / Right Bumper.
    constexpr std::uint8_t kLeftShiftModifier = 0x02;
    constexpr std::uint8_t kRightShiftModifier = 0x20;
    if (
        keyboard != nullptr &&
        keyboard->connected &&
        (keyboard->modifiers &
            (kLeftShiftModifier | kRightShiftModifier)) != 0
    ) {
        output.buttons |= ButtonRightBumper;
    }

    output = extraBindings_.apply(
        keyboard,
        mouse,
        output
    );

    // V6 +20% over V5 fine-tuned ADS compensation: games commonly apply a lower look sensitivity
    // while Left Trigger is held. Right mouse is the FPS profile's LT/ADS
    // source, so compensate only during ADS instead of globally destroying
    // micro-aim precision.
    const bool adsActive =
        mouse != nullptr &&
        mouse->connected &&
        (mouse->buttons & MouseButtonRight) != 0;

    constexpr double kAdsMotionBoost = 2.90;

    const StickVector aim =
        mouseMapper_.map(
            mouseMotion,
            mouseConfig_,
            mouseMotionScale *
                (adsActive ? kAdsMotionBoost : 1.0)
        );

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
