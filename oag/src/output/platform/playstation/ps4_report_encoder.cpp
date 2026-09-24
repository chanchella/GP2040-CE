#include "oag/output/platform/playstation/ps4_report_encoder.h"

#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {

Ps4InputReport Ps4ReportEncoder::encode(
    const LogicalGamepadState& state,
    std::uint8_t reportCounter
) const {
    Ps4InputReport out {};

    out.bytes[0] = 0x01;
    out.bytes[1] = axisTo8(state.lx);
    out.bytes[2] = axisTo8(state.ly);
    out.bytes[3] = axisTo8(state.rx);
    out.bytes[4] = axisTo8(state.ry);

    // Byte 5: low nibble = hat; high nibble = Square/Cross/Circle/Triangle.
    out.bytes[5] = dpadHat(state.dpad);

    if (state.buttons & ButtonWest) {
        out.bytes[5] |= 1u << 4; // Square
    }
    if (state.buttons & ButtonSouth) {
        out.bytes[5] |= 1u << 5; // Cross
    }
    if (state.buttons & ButtonEast) {
        out.bytes[5] |= 1u << 6; // Circle
    }
    if (state.buttons & ButtonNorth) {
        out.bytes[5] |= 1u << 7; // Triangle
    }

    // Byte 6: L1/R1/L2/R2/Share/Options/L3/R3.
    if (state.buttons & ButtonLeftBumper) out.bytes[6] |= 1u << 0;
    if (state.buttons & ButtonRightBumper) out.bytes[6] |= 1u << 1;
    if (state.leftTrigger != 0) out.bytes[6] |= 1u << 2;
    if (state.rightTrigger != 0) out.bytes[6] |= 1u << 3;
    if (state.buttons & ButtonBack) out.bytes[6] |= 1u << 4;
    if (state.buttons & ButtonStart) out.bytes[6] |= 1u << 5;
    if (state.buttons & ButtonLeftStick) out.bytes[6] |= 1u << 6;
    if (state.buttons & ButtonRightStick) out.bytes[6] |= 1u << 7;

    // Byte 7: PS, Touchpad, then 6-bit report counter.
    if (state.buttons & ButtonGuide) out.bytes[7] |= 1u << 0;
    out.bytes[7] |= static_cast<std::uint8_t>(
        (reportCounter & 0x3Fu) << 2u
    );

    out.bytes[8] = triggerTo8(state.leftTrigger);
    out.bytes[9] = triggerTo8(state.rightTrigger);

    // bytes 10..63 are vendor-specific sensor/touch payload.
    // U4D leaves them neutral until motion/touch foundations are added.
    return out;
}

std::uint8_t Ps4ReportEncoder::axisTo8(std::int32_t value) {
    if (value <= std::numeric_limits<std::int32_t>::min()) {
        return 0x00;
    }

    if (value >= std::numeric_limits<std::int32_t>::max()) {
        return 0xFF;
    }

    const std::uint64_t shifted =
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value) -
            std::numeric_limits<std::int32_t>::min()
        );

    return static_cast<std::uint8_t>(
        (shifted * 0xFFu) /
        std::numeric_limits<std::uint32_t>::max()
    );
}

std::uint8_t Ps4ReportEncoder::triggerTo8(std::uint32_t value) {
    return static_cast<std::uint8_t>(value >> 24u);
}

std::uint8_t Ps4ReportEncoder::dpadHat(std::uint8_t dpad) {
    const bool up =
        (dpad & static_cast<std::uint8_t>(DpadBits::Up)) != 0;
    const bool down =
        (dpad & static_cast<std::uint8_t>(DpadBits::Down)) != 0;
    const bool left =
        (dpad & static_cast<std::uint8_t>(DpadBits::Left)) != 0;
    const bool right =
        (dpad & static_cast<std::uint8_t>(DpadBits::Right)) != 0;

    if (up && right) return 0x01;
    if (right && down) return 0x03;
    if (down && left) return 0x05;
    if (left && up) return 0x07;
    if (up) return 0x00;
    if (right) return 0x02;
    if (down) return 0x04;
    if (left) return 0x06;

    return 0x0F;
}

} // namespace oag
