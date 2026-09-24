#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/digital_binding_engine.h"

using namespace oag;

int main() {
    DigitalBindingEngine engine;

    assert(engine.addBinding(
        keyboardUsage(0x1A), // W
        LogicalDigitalControl::LeftStickUp
    ));
    assert(engine.addBinding(
        keyboardUsage(0x16), // S
        LogicalDigitalControl::LeftStickDown
    ));
    assert(engine.addBinding(
        keyboardUsage(0x04), // A
        LogicalDigitalControl::LeftStickLeft
    ));
    assert(engine.addBinding(
        keyboardUsage(0x07), // D
        LogicalDigitalControl::LeftStickRight
    ));
    assert(engine.addBinding(
        keyboardUsage(0x2C), // Space
        LogicalDigitalControl::South
    ));
    assert(engine.addBinding(
        keyboardUsage(0x2C), // one-to-many
        LogicalDigitalControl::East
    ));
    assert(engine.addBinding(
        mouseButton(MouseButtonLeft),
        LogicalDigitalControl::RightTrigger
    ));

    assert(engine.count() == 7);

    KeyboardState keyboard {};
    keyboard.connected = true;
    keyboard.setPressed(0x1A, true);
    keyboard.setPressed(0x07, true);
    keyboard.setPressed(0x2C, true);

    MouseState mouse {};
    mouse.connected = true;
    mouse.buttons = MouseButtonLeft;

    LogicalGamepadState mapped =
        engine.apply(&keyboard, &mouse);

    assert(mapped.ly == std::numeric_limits<std::int32_t>::min());
    assert(mapped.lx == std::numeric_limits<std::int32_t>::max());
    assert((mapped.buttons & ButtonSouth) != 0);
    assert((mapped.buttons & ButtonEast) != 0);
    assert(mapped.rightTrigger ==
        std::numeric_limits<std::uint32_t>::max());

    // Opposing digital directions resolve to neutral deterministically.
    keyboard.setPressed(0x16, true);
    mapped = engine.apply(&keyboard, &mouse);
    assert(mapped.ly == 0);

    // If no mapped direction is active, preserve an existing/base axis.
    keyboard.setPressed(0x1A, false);
    keyboard.setPressed(0x16, false);
    keyboard.setPressed(0x07, false);

    LogicalGamepadState base {};
    base.lx = 123456;
    base.ly = -654321;

    mapped = engine.apply(&keyboard, nullptr, base);
    assert(mapped.lx == 123456);
    assert(mapped.ly == -654321);

    assert(engine.removeBinding(
        keyboardUsage(0x2C),
        LogicalDigitalControl::East
    ));
    assert(engine.count() == 6);

    assert(engine.removeSource(keyboardUsage(0x2C)) == 1);
    assert(engine.count() == 5);

    engine.clear();
    assert(engine.count() == 0);

    // Fixed capacity: no heap growth past the declared limit.
    for (std::size_t i = 0;
         i < DigitalBindingEngine::kMaxBindings;
         ++i) {
        assert(engine.addBinding(
            keyboardUsage(static_cast<std::uint8_t>(i)),
            LogicalDigitalControl::South
        ));
    }

    assert(!engine.addBinding(
        mouseButton(MouseButtonRight),
        LogicalDigitalControl::East
    ));

    return 0;
}
