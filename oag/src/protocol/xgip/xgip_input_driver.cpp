#include "oag/protocol/xgip/xgip_input_driver.h"

#include <cstdint>
#include <limits>

namespace oag {
namespace {

std::int16_t readS16(
    const std::uint8_t* report,
    std::size_t offset
) {
    const std::uint16_t raw =
        static_cast<std::uint16_t>(report[offset]) |
        (static_cast<std::uint16_t>(report[offset + 1]) << 8u);

    return static_cast<std::int16_t>(raw);
}

std::uint16_t readU16(
    const std::uint8_t* report,
    std::size_t offset
) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(report[offset]) |
        (static_cast<std::uint16_t>(report[offset + 1]) << 8u)
    );
}

} // namespace

bool XgipInputDriver::parse(
    DeviceId source,
    const std::uint8_t* report,
    std::size_t length,
    std::uint64_t timestampUs,
    UniversalGamepadState& output
) const {
    if (!source.valid() || report == nullptr || length < 18) {
        return false;
    }

    if (report[0] != 0x20) {
        return false;
    }

    UniversalGamepadState next {};
    next.source = source;
    next.connected = true;
    next.generation = output.generation + 1u;
    next.timestampUs = timestampUs;

    const std::uint16_t buttons =
        static_cast<std::uint16_t>(report[4]) |
        (static_cast<std::uint16_t>(report[5]) << 8u);

    if (buttons & (1u << 8)) {
        next.dpad |= static_cast<std::uint8_t>(DpadBits::Up);
    }
    if (buttons & (1u << 9)) {
        next.dpad |= static_cast<std::uint8_t>(DpadBits::Down);
    }
    if (buttons & (1u << 10)) {
        next.dpad |= static_cast<std::uint8_t>(DpadBits::Left);
    }
    if (buttons & (1u << 11)) {
        next.dpad |= static_cast<std::uint8_t>(DpadBits::Right);
    }

    if (buttons & (1u << 2)) next.buttons |= ButtonStart;
    if (buttons & (1u << 3)) next.buttons |= ButtonBack;

    if (buttons & (1u << 4)) next.buttons |= ButtonSouth;
    if (buttons & (1u << 5)) next.buttons |= ButtonEast;
    if (buttons & (1u << 6)) next.buttons |= ButtonWest;
    if (buttons & (1u << 7)) next.buttons |= ButtonNorth;

    if (buttons & (1u << 12)) next.buttons |= ButtonLeftBumper;
    if (buttons & (1u << 13)) next.buttons |= ButtonRightBumper;
    if (buttons & (1u << 14)) next.buttons |= ButtonLeftStick;
    if (buttons & (1u << 15)) next.buttons |= ButtonRightStick;

    next.leftTrigger = expandTrigger10(readU16(report, 6));
    next.rightTrigger = expandTrigger10(readU16(report, 8));

    next.lx = expandAxis(readS16(report, 10));
    next.ly = expandYAxis(readS16(report, 12));
    next.rx = expandAxis(readS16(report, 14));
    next.ry = expandYAxis(readS16(report, 16));

    output = next;
    return true;
}

std::int32_t XgipInputDriver::expandAxis(std::int16_t value) {
    if (value == std::numeric_limits<std::int16_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }

    if (value < 0) {
        return static_cast<std::int32_t>(value) * 65536;
    }

    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(value) *
        std::numeric_limits<std::int32_t>::max() /
        std::numeric_limits<std::int16_t>::max()
    );
}

std::int32_t XgipInputDriver::expandYAxis(std::int16_t value) {
    if (value == std::numeric_limits<std::int16_t>::max()) {
        return std::numeric_limits<std::int32_t>::min();
    }

    if (value == std::numeric_limits<std::int16_t>::min()) {
        return std::numeric_limits<std::int32_t>::max();
    }

    return expandAxis(static_cast<std::int16_t>(-value));
}

std::uint32_t XgipInputDriver::expandTrigger10(std::uint16_t value) {
    if (value > 0x03FFu) {
        value = 0x03FFu;
    }

    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(value) *
        std::numeric_limits<std::uint32_t>::max() /
        0x03FFu
    );
}

} // namespace oag
