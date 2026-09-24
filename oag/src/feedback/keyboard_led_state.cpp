#include "oag/feedback/keyboard_led_state.h"

namespace oag {
namespace {

constexpr std::uint8_t kUsageCapsLock = 0x39;
constexpr std::uint8_t kUsageScrollLock = 0x47;
constexpr std::uint8_t kUsageNumLock = 0x53;

} // namespace

KeyboardLedState::KeyboardLedState() {
    reset();
}

void KeyboardLedState::reset() {
    // PC-style default: numeric keypad enabled. This also provides immediate
    // visible confirmation that OAG's keyboard Output Report path is alive.
    leds_ = KeyboardLedNumLock;
}

bool KeyboardLedState::risingEdge(
    const KeyboardState& previous,
    const KeyboardState& current,
    std::uint8_t usage
) {
    return
        current.pressed(usage) &&
        !previous.pressed(usage);
}

bool KeyboardLedState::updateFromKeyEdges(
    const KeyboardState& previous,
    const KeyboardState& current
) {
    const std::uint8_t before = leds_;

    if (risingEdge(previous, current, kUsageNumLock)) {
        leds_ ^= KeyboardLedNumLock;
    }

    if (risingEdge(previous, current, kUsageCapsLock)) {
        leds_ ^= KeyboardLedCapsLock;
    }

    if (risingEdge(previous, current, kUsageScrollLock)) {
        leds_ ^= KeyboardLedScrollLock;
    }

    return leds_ != before;
}

} // namespace oag
