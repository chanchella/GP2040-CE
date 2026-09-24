#include <cassert>

#include "oag/feedback/keyboard_led_state.h"
#include "oag/input/keyboard_state.h"

using namespace oag;

int main() {
    KeyboardLedState leds;

    assert(
        (leds.reportByte() & KeyboardLedNumLock) != 0
    );
    assert(
        (leds.reportByte() & KeyboardLedCapsLock) == 0
    );

    KeyboardState previous {};
    KeyboardState current {};

    current.setPressed(0x39, true); // Caps Lock
    assert(leds.updateFromKeyEdges(previous, current));
    assert(
        (leds.reportByte() & KeyboardLedCapsLock) != 0
    );

    // Held key is not another edge.
    previous = current;
    assert(!leds.updateFromKeyEdges(previous, current));

    // Release itself doesn't toggle.
    previous = current;
    current.setPressed(0x39, false);
    assert(!leds.updateFromKeyEdges(previous, current));

    // Press again toggles off.
    previous = current;
    current.setPressed(0x39, true);
    assert(leds.updateFromKeyEdges(previous, current));
    assert(
        (leds.reportByte() & KeyboardLedCapsLock) == 0
    );

    // Num Lock starts on and toggles off.
    previous = {};
    current = {};
    current.setPressed(0x53, true);
    assert(leds.updateFromKeyEdges(previous, current));
    assert(
        (leds.reportByte() & KeyboardLedNumLock) == 0
    );

    // Scroll lock independent bit.
    previous = {};
    current = {};
    current.setPressed(0x47, true);
    assert(leds.updateFromKeyEdges(previous, current));
    assert(
        (leds.reportByte() & KeyboardLedScrollLock) != 0
    );

    leds.reset();
    assert(leds.reportByte() == KeyboardLedNumLock);

    return 0;
}
