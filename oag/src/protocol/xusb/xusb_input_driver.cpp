#include "oag/protocol/xusb/xusb_input_driver.h"

#include <cstdint>
#include <limits>

namespace oag {
namespace {

std::int16_t readS16(const std::uint8_t* report, std::size_t offset) {
    const std::uint16_t raw =
        static_cast<std::uint16_t>(report[offset]) |
        (static_cast<std::uint16_t>(report[offset + 1]) << 8);
    return static_cast<std::int16_t>(raw);
}

} // namespace

bool XusbInputDriver::parse(
    DeviceId source,
    const std::uint8_t* report,
    std::size_t length,
    std::uint64_t timestampUs,
    UniversalGamepadState& output
) const {
    if (!source.valid() || report == nullptr || length < 14) {
        return false;
    }

    // Proven G2E3/T29 compatibility rule:
    // byte 1 carries the normal Xbox 360 gameplay packet length (0x14).
    // byte 0 is deliberately not required because compatible clones vary it.
    if (report[1] != 0x14) {
        return false;
    }

    const std::uint16_t buttons =
        static_cast<std::uint16_t>(report[2]) |
        (static_cast<std::uint16_t>(report[3]) << 8);

    UniversalGamepadState next {};
    next.source = source;
    next.connected = true;
    next.timestampUs = timestampUs;
    next.generation = output.generation + 1u;

    if (buttons & 0x0001u) next.dpad |= static_cast<std::uint8_t>(DpadBits::Up);
    if (buttons & 0x0002u) next.dpad |= static_cast<std::uint8_t>(DpadBits::Down);
    if (buttons & 0x0004u) next.dpad |= static_cast<std::uint8_t>(DpadBits::Left);
    if (buttons & 0x0008u) next.dpad |= static_cast<std::uint8_t>(DpadBits::Right);

    if (buttons & 0x0010u) next.buttons |= ButtonStart;
    if (buttons & 0x0020u) next.buttons |= ButtonBack;
    if (buttons & 0x0040u) next.buttons |= ButtonLeftStick;
    if (buttons & 0x0080u) next.buttons |= ButtonRightStick;
    if (buttons & 0x0100u) next.buttons |= ButtonLeftBumper;
    if (buttons & 0x0200u) next.buttons |= ButtonRightBumper;
    if (buttons & 0x0400u) next.buttons |= ButtonGuide;

    if (buttons & 0x1000u) next.buttons |= ButtonSouth;
    if (buttons & 0x2000u) next.buttons |= ButtonEast;
    if (buttons & 0x4000u) next.buttons |= ButtonWest;
    if (buttons & 0x8000u) next.buttons |= ButtonNorth;

    next.leftTrigger = normalizeTrigger(report[4]);
    next.rightTrigger = normalizeTrigger(report[5]);

    next.lx = normalizeAxis(readS16(report, 6));
    next.ly = normalizeYAxis(readS16(report, 8));
    next.rx = normalizeAxis(readS16(report, 10));
    next.ry = normalizeYAxis(readS16(report, 12));

    output = next;
    return true;
}

std::int32_t XusbInputDriver::normalizeAxis(std::int16_t value) {
    if (value < 0) {
        return static_cast<std::int32_t>(value) * 65536;
    }

    if (value == 0) {
        return 0;
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) *
        std::numeric_limits<std::int32_t>::max() /
        std::numeric_limits<std::int16_t>::max();

    return static_cast<std::int32_t>(scaled);
}

std::int32_t XusbInputDriver::normalizeYAxis(std::int16_t value) {
    const std::int32_t normalized = normalizeAxis(value);

    // XUSB reports positive Y upward, while the canonical OAG/HID-facing
    // orientation follows G2E3: upward is negative and downward is positive.
    // Saturate the one asymmetric two's-complement endpoint instead of
    // overflowing when negating INT32_MIN.
    if (normalized == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::max();
    }

    return -normalized;
}

std::uint32_t XusbInputDriver::normalizeTrigger(std::uint8_t value) {
    return static_cast<std::uint32_t>(value) * 0x01010101u;
}

} // namespace oag
