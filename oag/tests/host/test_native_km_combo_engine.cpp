#include <cassert>

#include "oag/mapping/native_km_combo_engine.h"

using namespace oag;

int main() {
    NativeKmComboEngine engine;

    KeyboardState keyboard {};
    keyboard.connected = true;
    keyboard.modifiers = 0x01; // Left Ctrl.
    keyboard.setPressed(0x1E, true); // Keyboard 1.

    MouseState mouse {};
    mouse.connected = true;
    mouse.buttons = MouseButtonBack;

    NativeKmComboRule combo {};
    combo.requiredModifiers = 0x01;
    combo.requiredKeys[0] = 0x1E;
    combo.requiredMouseButtons = MouseButtonBack;
    combo.outputModifiers = 0x02; // Left Shift.
    combo.outputKeys[0] = 0x06; // C.
    combo.outputMouseButtons = MouseButtonForward;
    combo.suppressTriggerInputs = true;

    assert(engine.configure(0, combo));

    const NativeKmComboFrame output =
        engine.apply(keyboard, mouse);

    assert((output.keyboard.modifiers & 0x01) == 0);
    assert((output.keyboard.modifiers & 0x02) != 0);
    assert(!output.keyboard.pressed(0x1E));
    assert(output.keyboard.pressed(0x06));
    assert((output.mouse.buttons & MouseButtonBack) == 0);
    assert((output.mouse.buttons & MouseButtonForward) != 0);

    assert(engine.disable(0));
    const NativeKmComboFrame passthrough =
        engine.apply(keyboard, mouse);
    assert(passthrough.keyboard.pressed(0x1E));
    assert((passthrough.mouse.buttons & MouseButtonBack) != 0);

    return 0;
}
