#include "oag/output/platform/playstation/ps3_report_encoder.h"

#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::uint8_t kPlugged = 0x02;
constexpr std::uint8_t kPowerFull = 0x05;
constexpr std::uint8_t kWiredRumble = 0x10;
constexpr std::uint16_t kSixaxisCenter = 0xFF01;

constexpr bool pressed(
    const LogicalGamepadState& state,
    std::uint64_t mask
) {
    return (state.buttons & mask) != 0;
}

constexpr std::uint8_t pressure(bool down) {
    return down ? 0xFF : 0x00;
}

} // namespace

Ps3Ds3Report Ps3ReportEncoder::encode(
    const LogicalGamepadState& state
) const {
    Ps3Ds3Report out {};
    auto& b = out.bytes;

    b[0] = 0x01; // Report ID
    b[1] = 0x00; // Reserved

    // Byte 2:
    // Select, L3, R3, Start, Up, Right, Down, Left.
    if (pressed(state, ButtonBack)) b[2] |= 1u << 0;
    if (pressed(state, ButtonLeftStick)) b[2] |= 1u << 1;
    if (pressed(state, ButtonRightStick)) b[2] |= 1u << 2;
    if (pressed(state, ButtonStart)) b[2] |= 1u << 3;

    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) {
        b[2] |= 1u << 4;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) {
        b[2] |= 1u << 5;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) {
        b[2] |= 1u << 6;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) {
        b[2] |= 1u << 7;
    }

    // Byte 3:
    // L2, R2, L1, R1, Triangle, Circle, Cross, Square.
    if (state.leftTrigger != 0) b[3] |= 1u << 0;
    if (state.rightTrigger != 0) b[3] |= 1u << 1;
    if (pressed(state, ButtonLeftBumper)) b[3] |= 1u << 2;
    if (pressed(state, ButtonRightBumper)) b[3] |= 1u << 3;
    if (pressed(state, ButtonNorth)) b[3] |= 1u << 4;
    if (pressed(state, ButtonEast)) b[3] |= 1u << 5;
    if (pressed(state, ButtonSouth)) b[3] |= 1u << 6;
    if (pressed(state, ButtonWest)) b[3] |= 1u << 7;

    if (pressed(state, ButtonGuide)) {
        b[4] |= 1u << 0; // PS
    }

    // Byte 5 remains reserved.
    b[6] = axisTo8(state.lx);
    b[7] = axisTo8(state.ly);
    b[8] = axisTo8(state.rx);
    b[9] = axisTo8(state.ry);

    // Byte 12 is Move power status in the Golden semantic structure.
    b[12] = 0x00;

    b[14] = pressure(
        (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) != 0
    );
    b[15] = pressure(
        (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) != 0
    );
    b[16] = pressure(
        (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) != 0
    );
    b[17] = pressure(
        (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) != 0
    );

    b[18] = triggerTo8(state.leftTrigger);
    b[19] = triggerTo8(state.rightTrigger);
    b[20] = pressure(pressed(state, ButtonLeftBumper));
    b[21] = pressure(pressed(state, ButtonRightBumper));

    b[22] = pressure(pressed(state, ButtonNorth));
    b[23] = pressure(pressed(state, ButtonEast));
    b[24] = pressure(pressed(state, ButtonSouth));
    b[25] = pressure(pressed(state, ButtonWest));

    b[29] = kPlugged;
    b[30] = kPowerFull;
    b[31] = kWiredRumble;

    writeLe16(b, 41, kSixaxisCenter);
    writeLe16(b, 43, kSixaxisCenter);
    writeLe16(b, 45, kSixaxisCenter);
    writeLe16(b, 47, kSixaxisCenter);
    writeLe16(b, 49, kSixaxisCenter);

    return out;
}

std::uint8_t Ps3ReportEncoder::axisTo8(std::int32_t value) {
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

std::uint8_t Ps3ReportEncoder::triggerTo8(std::uint32_t value) {
    return static_cast<std::uint8_t>(value >> 24u);
}

void Ps3ReportEncoder::writeLe16(
    std::array<std::uint8_t, Ps3Ds3Report::kSize>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

} // namespace oag
