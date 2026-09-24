#include "oag/protocol/hid/boot_mouse_input_driver.h"

#include <cstdint>

namespace oag {

bool BootMouseInputDriver::parse(
    DeviceId source,
    const std::uint8_t* report,
    std::size_t length,
    std::uint64_t timestampUs,
    MouseState& output
) const {
    if (!source.valid() || report == nullptr || length < 3) {
        return false;
    }

    MouseState next {};
    next.source = source;
    next.connected = true;
    next.buttons = report[0];
    next.dx = static_cast<std::int8_t>(report[1]);
    next.dy = static_cast<std::int8_t>(report[2]);
    next.wheel = length >= 4
        ? static_cast<std::int8_t>(report[3])
        : 0;
    next.pan = length >= 5
        ? static_cast<std::int8_t>(report[4])
        : 0;
    next.generation = output.generation + 1u;
    next.timestampUs = timestampUs;

    output = next;
    return true;
}

} // namespace oag
