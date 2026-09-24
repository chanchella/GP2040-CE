#include "oag/protocol/hid/boot_keyboard_input_driver.h"

namespace oag {

bool BootKeyboardInputDriver::parse(
    DeviceId source,
    const std::uint8_t* report,
    std::size_t length,
    std::uint64_t timestampUs,
    KeyboardState& output
) const {
    if (!source.valid() || report == nullptr || length < 8) {
        return false;
    }

    // Boot keyboard report:
    // [0] modifier, [1] reserved, [2..7] six active usages.
    // 0x01..0x03 are HID error/rollover usages, not real key presses.
    for (std::size_t i = 2; i < 8; ++i) {
        if (report[i] >= 0x01 && report[i] <= 0x03) {
            return false;
        }
    }

    KeyboardState next {};
    next.source = source;
    next.connected = true;
    next.modifiers = report[0];
    next.generation = output.generation + 1u;
    next.timestampUs = timestampUs;

    for (std::size_t i = 2; i < 8; ++i) {
        const std::uint8_t usage = report[i];
        if (usage != 0) {
            next.setPressed(usage, true);
        }
    }

    output = next;
    return true;
}

} // namespace oag
