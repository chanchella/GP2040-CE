#pragma once

#include <cstdint>

#include "oag/input/keyboard_state.h"

namespace oag {

enum KeyboardLedBit : std::uint8_t {
    KeyboardLedNumLock = 1u << 0,
    KeyboardLedCapsLock = 1u << 1,
    KeyboardLedScrollLock = 1u << 2,
};

class KeyboardLedState {
public:
    KeyboardLedState();

    bool updateFromKeyEdges(
        const KeyboardState& previous,
        const KeyboardState& current
    );

    std::uint8_t reportByte() const {
        return leds_;
    }

    void reset();

private:
    std::uint8_t leds_ = KeyboardLedNumLock;

    static bool risingEdge(
        const KeyboardState& previous,
        const KeyboardState& current,
        std::uint8_t usage
    );
};

} // namespace oag
