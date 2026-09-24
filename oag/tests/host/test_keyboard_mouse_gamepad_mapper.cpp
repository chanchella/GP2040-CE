#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"

using namespace oag;

int main() {
    KeyboardMouseGamepadMapper mapper;

    KeyboardState keyboard {};
    keyboard.connected = true;

    MouseState mouse {};
    mouse.connected = true;

    // W + D + Space -> up/right left-stick + South.
    keyboard.setPressed(0x1A, true);
    keyboard.setPressed(0x07, true);
    keyboard.setPressed(0x2C, true);

    mouse.buttons =
        MouseButtonLeft |
        MouseButtonRight;

    LogicalGamepadState output = mapper.apply(
        &keyboard,
        &mouse,
        MouseMotion {10, -5}
    );

    assert(output.connected);
    assert(output.lx == std::numeric_limits<std::int32_t>::max());
    assert(output.ly == std::numeric_limits<std::int32_t>::min());
    assert((output.buttons & ButtonSouth) != 0);
    assert(output.rightTrigger ==
        std::numeric_limits<std::uint32_t>::max());
    assert(output.leftTrigger ==
        std::numeric_limits<std::uint32_t>::max());

    assert(output.rx > 0);
    assert(output.ry < 0);

    // Mouse aim is a pulse. A zero delta must leave an existing base right
    // stick untouched; the firmware decides when to recenter.
    LogicalGamepadState base {};
    base.connected = true;
    base.rx = 1234;
    base.ry = -5678;

    keyboard = {};
    mouse = {};
    mouse.connected = true;

    output = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {0, 0},
        base
    );

    assert(output.rx == 1234);
    assert(output.ry == -5678);

    // Opposing WASD directions neutralize deterministically.
    keyboard.connected = true;
    keyboard.setPressed(0x1A, true);
    keyboard.setPressed(0x16, true);

    output = mapper.apply(
        &keyboard,
        nullptr,
        MouseMotion {}
    );

    assert(output.ly == 0);

    // Arrow Right -> D-pad right.
    keyboard = {};
    keyboard.connected = true;
    keyboard.setPressed(0x4F, true);

    output = mapper.apply(
        &keyboard,
        nullptr,
        MouseMotion {}
    );

    assert(
        output.dpad ==
        static_cast<std::uint8_t>(DpadBits::Right)
    );

    return 0;
}
