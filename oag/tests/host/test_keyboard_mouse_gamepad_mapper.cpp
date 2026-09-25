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


    // UI4: six isolated extra bind slots exist and default to four keyboard
    // keys plus the two mouse side buttons.
    assert(KeyboardMouseGamepadMapper::kExtraBindSlots == 6);

    for (std::size_t i = 0;
         i < KeyboardMouseGamepadMapper::kExtraBindSlots;
         ++i) {
        const ExtraBindSlot* slot = mapper.extraBind(i);
        assert(slot != nullptr);
        assert(slot->enabled);
    }

    const ExtraBindSlot* extra0 = mapper.extraBind(0);
    const ExtraBindSlot* extra4 = mapper.extraBind(4);
    const ExtraBindSlot* extra5 = mapper.extraBind(5);

    assert(extra0->source == keyboardUsage(0x3A));
    assert(extra4->source == mouseButton(MouseButtonBack));
    assert(extra5->source == mouseButton(MouseButtonForward));

    keyboard = {};
    keyboard.connected = true;
    keyboard.setPressed(0x3A, true); // F1 default -> Guide.

    output = mapper.apply(
        &keyboard,
        nullptr,
        MouseMotion {}
    );

    assert((output.buttons & ButtonGuide) != 0);

    // Extra slots are genuinely reconfigurable without touching the base
    // keyboard/mouse profile.
    assert(mapper.configureExtraBind(
        0,
        keyboardUsage(0x3A),
        LogicalDigitalControl::North
    ));

    output = mapper.apply(
        &keyboard,
        nullptr,
        MouseMotion {}
    );

    assert((output.buttons & ButtonNorth) != 0);
    assert((output.buttons & ButtonGuide) == 0);

    mouse = {};
    mouse.connected = true;
    mouse.buttons =
        MouseButtonBack |
        MouseButtonForward;

    output = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {}
    );

    assert((output.buttons & ButtonLeftBumper) != 0);
    assert((output.buttons & ButtonRightBumper) != 0);

    // UI4D 60%-launch curve: one mouse count should land very close to
    // 60% right-stick travel.
    output = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {1, 0}
    );

    const std::int64_t oneCount =
        static_cast<std::int64_t>(output.rx);
    const std::int64_t fullScale =
        static_cast<std::int64_t>(
            std::numeric_limits<std::int32_t>::max()
        );

    assert(oneCount > (fullScale * 59) / 100);  // > 59%
    assert(oneCount < (fullScale * 61) / 100);  // < 61%

    return 0;
}
