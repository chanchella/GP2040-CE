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
    // number keys plus the two mouse side buttons.
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

    assert(extra0->source == keyboardUsage(0x1E));
    assert(extra4->source == mouseButton(MouseButtonBack));
    assert(extra5->source == mouseButton(MouseButtonForward));

    keyboard = {};
    keyboard.connected = true;
    keyboard.setPressed(0x1E, true); // 1 default -> Guide.

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
        keyboardUsage(0x1E),
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

    // Legendary Aim V1: a one-count 1 kHz micro movement must remain
    // controllable instead of slamming the virtual right stick to 100%.
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

    // V5 keeps V4's fast response and raises base sensitivity exactly 15%. At the 1 kHz reference
    // interval, even one whole mouse count is intentionally full-stick.
    assert(oneCount == fullScale);

    // A real flick still reaches full stick travel quickly.
    output = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {2, 0}
    );
    assert(output.rx == std::numeric_limits<std::int32_t>::max());

    // Equal physical velocity at 125 Hz and 1000 Hz must map identically.
    const auto fastPoll = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {1, 0},
        {},
        1.0
    );
    const auto slowPoll = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {8, 0},
        {},
        0.125
    );
    assert(fastPoll.rx == slowPoll.rx);

    // V5: ADS inherits the same +15% base increase while the proven 2.90x ADS
    // boost stays unchanged. Use a fractional scale so both remain distinguishable.
    mouse.buttons = 0;
    const auto hipAim = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {1, 0},
        {},
        0.25
    );

    mouse.buttons = MouseButtonRight;
    const auto adsAim = mapper.apply(
        nullptr,
        &mouse,
        MouseMotion {1, 0},
        {},
        0.25
    );

    assert(adsAim.leftTrigger ==
        std::numeric_limits<std::uint32_t>::max());
    assert(adsAim.rx > hipAim.rx);
    assert(adsAim.rx <= std::numeric_limits<std::int32_t>::max());

    return 0;
}
