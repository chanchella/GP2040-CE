#include <cassert>
#include <cstdint>

#include "oag/device/device_id.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/protocol/hid/boot_keyboard_input_driver.h"
#include "oag/protocol/hid/boot_mouse_input_driver.h"

using namespace oag;

int main() {
    const DeviceId keyboardId {2, 1};
    BootKeyboardInputDriver keyboardDriver;
    KeyboardState keyboard {};

    const std::uint8_t keyboardReport[8] = {
        0x02, 0x00, // Left Shift
        0x04,       // A
        0x16,       // S
        0x00, 0x00, 0x00, 0x00
    };

    assert(keyboardDriver.parse(
        keyboardId,
        keyboardReport,
        sizeof(keyboardReport),
        1000,
        keyboard
    ));
    assert(keyboard.connected);
    assert(keyboard.source == keyboardId);
    assert(keyboard.modifiers == 0x02);
    assert(keyboard.pressed(0x04));
    assert(keyboard.pressed(0x16));
    assert(!keyboard.pressed(0x07));
    assert(keyboard.generation == 1);
    assert(keyboard.timestampUs == 1000);

    const std::uint8_t keyboardRelease[8] = {
        0x00, 0x00,
        0x07,       // D only
        0x00, 0x00, 0x00, 0x00, 0x00
    };

    assert(keyboardDriver.parse(
        keyboardId,
        keyboardRelease,
        sizeof(keyboardRelease),
        2000,
        keyboard
    ));
    assert(!keyboard.pressed(0x04));
    assert(!keyboard.pressed(0x16));
    assert(keyboard.pressed(0x07));
    assert(keyboard.generation == 2);

    const std::uint8_t rollover[8] = {
        0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01
    };
    assert(!keyboardDriver.parse(
        keyboardId,
        rollover,
        sizeof(rollover),
        3000,
        keyboard
    ));
    assert(keyboard.generation == 2);

    const DeviceId mouseId {3, 1};
    BootMouseInputDriver mouseDriver;
    MouseState mouse {};

    const std::uint8_t mouseReport[5] = {
        0x05, // Left + Middle
        0x7F, // +127 X
        0x80, // -128 Y
        0x01, // wheel +1
        0xFF  // pan -1
    };

    assert(mouseDriver.parse(
        mouseId,
        mouseReport,
        sizeof(mouseReport),
        4000,
        mouse
    ));
    assert(mouse.connected);
    assert(mouse.source == mouseId);
    assert((mouse.buttons & MouseButtonLeft) != 0);
    assert((mouse.buttons & MouseButtonMiddle) != 0);
    assert(mouse.dx == 127);
    assert(mouse.dy == -128);
    assert(mouse.wheel == 1);
    assert(mouse.pan == -1);
    assert(mouse.generation == 1);
    assert(mouse.timestampUs == 4000);

    const std::uint8_t threeByteMouse[3] = {
        0x00,
        0xFF, // -1 X
        0x01  // +1 Y
    };

    assert(mouseDriver.parse(
        mouseId,
        threeByteMouse,
        sizeof(threeByteMouse),
        5000,
        mouse
    ));
    assert(mouse.dx == -1);
    assert(mouse.dy == 1);
    assert(mouse.wheel == 0);
    assert(mouse.pan == 0);
    assert(mouse.generation == 2);

    assert(!mouseDriver.parse(
        mouseId,
        threeByteMouse,
        2,
        6000,
        mouse
    ));
    assert(mouse.generation == 2);

    return 0;
}
