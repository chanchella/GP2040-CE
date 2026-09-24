#include "oag/output/platform/xbox/xbox_gip_report_encoder.h"

#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/platform/xbox/xgip_packet.h"

namespace oag {

XboxGipInputPacket XboxGipReportEncoder::encodeInput(
    const LogicalGamepadState& state,
    std::uint8_t sequence
) const {
    XboxGipInputPacket out {};

    XgipPacketCodec::writeHeader(
        out.bytes.data(),
        XgipCommand::InputReport,
        sequence,
        32,
        false
    );

    // Payload byte 0:
    // sync, guide, start, back, A, B, X, Y.
    // Guide is intentionally zero in normal gameplay packets; the Golden
    // baseline emits Guide through GIP_VIRTUAL_KEYCODE.
    if (state.buttons & ButtonStart) out.bytes[4] |= 1u << 2;
    if (state.buttons & ButtonBack) out.bytes[4] |= 1u << 3;
    if (state.buttons & ButtonSouth) out.bytes[4] |= 1u << 4;
    if (state.buttons & ButtonEast) out.bytes[4] |= 1u << 5;
    if (state.buttons & ButtonWest) out.bytes[4] |= 1u << 6;
    if (state.buttons & ButtonNorth) out.bytes[4] |= 1u << 7;

    // Payload byte 1:
    // Up, Down, Left, Right, LB, RB, L3, R3.
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) {
        out.bytes[5] |= 1u << 0;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) {
        out.bytes[5] |= 1u << 1;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) {
        out.bytes[5] |= 1u << 2;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) {
        out.bytes[5] |= 1u << 3;
    }
    if (state.buttons & ButtonLeftBumper) out.bytes[5] |= 1u << 4;
    if (state.buttons & ButtonRightBumper) out.bytes[5] |= 1u << 5;
    if (state.buttons & ButtonLeftStick) out.bytes[5] |= 1u << 6;
    if (state.buttons & ButtonRightStick) out.bytes[5] |= 1u << 7;

    writeLe16(&out.bytes[6], triggerTo10(state.leftTrigger));
    writeLe16(&out.bytes[8], triggerTo10(state.rightTrigger));

    writeLe16(
        &out.bytes[10],
        static_cast<std::uint16_t>(axisTo16(state.lx))
    );
    writeLe16(
        &out.bytes[12],
        static_cast<std::uint16_t>(yAxisTo16(state.ly))
    );
    writeLe16(
        &out.bytes[14],
        static_cast<std::uint16_t>(axisTo16(state.rx))
    );
    writeLe16(
        &out.bytes[16],
        static_cast<std::uint16_t>(yAxisTo16(state.ry))
    );

    // bytes 18..35 are Golden-compatible reserved zeros.
    return out;
}

XboxGipVirtualKeyPacket XboxGipReportEncoder::encodeGuide(
    bool pressed,
    std::uint8_t sequence
) const {
    XboxGipVirtualKeyPacket out {};

    XgipPacketCodec::writeHeader(
        out.bytes.data(),
        XgipCommand::VirtualKeycode,
        sequence,
        2,
        true
    );

    out.bytes[4] = pressed ? 0x01 : 0x00;
    out.bytes[5] = 0x5B;

    return out;
}

XboxGipKeepAlivePacket XboxGipReportEncoder::encodeKeepAlive(
    std::uint8_t sequence
) const {
    XboxGipKeepAlivePacket out {};

    XgipPacketCodec::writeHeader(
        out.bytes.data(),
        XgipCommand::KeepAlive,
        sequence,
        4,
        true
    );

    out.bytes[4] = 0x80;
    out.bytes[5] = 0x00;
    out.bytes[6] = 0x00;
    out.bytes[7] = 0x00;

    return out;
}

std::int16_t XboxGipReportEncoder::axisTo16(
    std::int32_t value
) {
    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int16_t>::min();
    }

    if (value <= 0) {
        return static_cast<std::int16_t>(value / 65536);
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) *
        std::numeric_limits<std::int16_t>::max() /
        std::numeric_limits<std::int32_t>::max();

    return static_cast<std::int16_t>(scaled);
}

std::int16_t XboxGipReportEncoder::yAxisTo16(
    std::int32_t value
) {
    if (value == std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int16_t>::min();
    }

    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int16_t>::max();
    }

    return axisTo16(-value);
}

std::uint16_t XboxGipReportEncoder::triggerTo10(
    std::uint32_t value
) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint64_t>(value) * 0x03FFu) /
        std::numeric_limits<std::uint32_t>::max()
    );
}

void XboxGipReportEncoder::writeLe16(
    std::uint8_t* destination,
    std::uint16_t value
) {
    destination[0] =
        static_cast<std::uint8_t>(value & 0xFFu);
    destination[1] =
        static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

} // namespace oag
