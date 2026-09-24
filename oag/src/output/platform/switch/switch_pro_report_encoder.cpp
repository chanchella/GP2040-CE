#include "oag/output/platform/switch/switch_pro_report_encoder.h"

#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {

SwitchProInputReport SwitchProReportEncoder::encode(
    const LogicalGamepadState& state,
    std::uint8_t timestamp
) const {
    SwitchProInputReport out {};

    out.bytes[0] = 0x30;
    out.bytes[1] = timestamp;

    // Connection-info low nibble = 0, battery high nibble = 8.
    out.bytes[2] = 0x80;

    // Right-side face/shoulder byte:
    // Y X B A RightSR RightSL R ZR
    if (state.buttons & ButtonWest) out.bytes[3] |= 1u << 0; // Y
    if (state.buttons & ButtonNorth) out.bytes[3] |= 1u << 1; // X
    if (state.buttons & ButtonSouth) out.bytes[3] |= 1u << 2; // B
    if (state.buttons & ButtonEast) out.bytes[3] |= 1u << 3; // A
    if (state.buttons & ButtonRightBumper) out.bytes[3] |= 1u << 6;
    if (state.rightTrigger != 0) out.bytes[3] |= 1u << 7;

    // Shared/system byte:
    // Minus Plus R3 L3 Home Capture dummy charging-grip.
    if (state.buttons & ButtonBack) out.bytes[4] |= 1u << 0;
    if (state.buttons & ButtonStart) out.bytes[4] |= 1u << 1;
    if (state.buttons & ButtonRightStick) out.bytes[4] |= 1u << 2;
    if (state.buttons & ButtonLeftStick) out.bytes[4] |= 1u << 3;
    if (state.buttons & ButtonGuide) out.bytes[4] |= 1u << 4;
    out.bytes[4] |= 1u << 7; // charging grip / wired connection hint

    // Left-side / D-pad byte:
    // Down Up Right Left LeftSL LeftSR L ZL.
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) {
        out.bytes[5] |= 1u << 0;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) {
        out.bytes[5] |= 1u << 1;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) {
        out.bytes[5] |= 1u << 2;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) {
        out.bytes[5] |= 1u << 3;
    }
    if (state.buttons & ButtonLeftBumper) out.bytes[5] |= 1u << 6;
    if (state.leftTrigger != 0) out.bytes[5] |= 1u << 7;

    const std::uint16_t lx = axisTo12(state.lx);
    const std::uint16_t ly =
        static_cast<std::uint16_t>(0x0FFFu - axisTo12(state.ly));
    const std::uint16_t rx = axisTo12(state.rx);
    const std::uint16_t ry =
        static_cast<std::uint16_t>(0x0FFFu - axisTo12(state.ry));

    packStick(&out.bytes[6], lx, ly);
    packStick(&out.bytes[9], rx, ry);

    // Golden Switch Pro runtime uses 0x09 here while streaming report 0x30.
    out.bytes[12] = 0x09;

    return out;
}

std::uint16_t SwitchProReportEncoder::axisTo12(
    std::int32_t value
) {
    if (value <= std::numeric_limits<std::int32_t>::min()) {
        return 0;
    }

    if (value >= std::numeric_limits<std::int32_t>::max()) {
        return 0x0FFF;
    }

    const std::uint64_t shifted =
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value) -
            std::numeric_limits<std::int32_t>::min()
        );

    return static_cast<std::uint16_t>(
        (shifted * 0x0FFFu) /
        std::numeric_limits<std::uint32_t>::max()
    );
}

void SwitchProReportEncoder::packStick(
    std::uint8_t* destination,
    std::uint16_t x,
    std::uint16_t y
) {
    x &= 0x0FFF;
    y &= 0x0FFF;

    destination[0] = static_cast<std::uint8_t>(x & 0xFFu);
    destination[1] = static_cast<std::uint8_t>(
        ((x >> 8u) & 0x0Fu) |
        ((y & 0x0Fu) << 4u)
    );
    destination[2] = static_cast<std::uint8_t>(
        (y >> 4u) & 0xFFu
    );
}

} // namespace oag
