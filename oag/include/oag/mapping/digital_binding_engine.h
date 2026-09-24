#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

enum class BindingSourceKind : std::uint8_t {
    KeyboardUsage = 0,
    MouseButton = 1,
};

struct BindingSource {
    BindingSourceKind kind = BindingSourceKind::KeyboardUsage;
    std::uint16_t code = 0;

    friend constexpr bool operator==(
        BindingSource lhs,
        BindingSource rhs
    ) {
        return lhs.kind == rhs.kind && lhs.code == rhs.code;
    }
};

constexpr BindingSource keyboardUsage(std::uint8_t usage) {
    return {
        BindingSourceKind::KeyboardUsage,
        usage,
    };
}

constexpr BindingSource mouseButton(std::uint16_t buttonMask) {
    return {
        BindingSourceKind::MouseButton,
        buttonMask,
    };
}

enum class LogicalDigitalControl : std::uint8_t {
    South = 0,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    LeftStickClick,
    RightStickClick,
    Back,
    Start,
    Guide,

    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,

    LeftStickUp,
    LeftStickDown,
    LeftStickLeft,
    LeftStickRight,

    RightStickUp,
    RightStickDown,
    RightStickLeft,
    RightStickRight,

    LeftTrigger,
    RightTrigger,
};

struct DigitalBinding {
    bool used = false;
    BindingSource source {};
    LogicalDigitalControl target = LogicalDigitalControl::South;
};

class DigitalBindingEngine {
public:
    static constexpr std::size_t kMaxBindings = 64;

    bool addBinding(
        BindingSource source,
        LogicalDigitalControl target
    );

    bool removeBinding(
        BindingSource source,
        LogicalDigitalControl target
    );

    std::size_t removeSource(BindingSource source);
    void clear();

    std::size_t count() const;

    LogicalGamepadState apply(
        const KeyboardState* keyboard,
        const MouseState* mouse,
        LogicalGamepadState base = {}
    ) const;

private:
    bool sourceActive(
        BindingSource source,
        const KeyboardState* keyboard,
        const MouseState* mouse
    ) const;

    std::array<DigitalBinding, kMaxBindings> bindings_ {};
};

} // namespace oag
