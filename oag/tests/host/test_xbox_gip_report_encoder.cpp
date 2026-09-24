#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/platform/xbox/xbox_gip_report_encoder.h"
#include "oag/output/platform/xbox/xgip_packet.h"

using namespace oag;

namespace {

std::uint16_t readLe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        p[0] |
        (static_cast<std::uint16_t>(p[1]) << 8u)
    );
}

} // namespace

int main() {
    XboxGipReportEncoder encoder;
    XgipPacketCodec codec;

    LogicalGamepadState state {};
    state.connected = true;
    state.buttons =
        ButtonSouth |
        ButtonEast |
        ButtonWest |
        ButtonNorth |
        ButtonLeftBumper |
        ButtonRightBumper |
        ButtonLeftStick |
        ButtonRightStick |
        ButtonBack |
        ButtonStart |
        ButtonGuide;

    state.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Right);

    state.leftTrigger =
        std::numeric_limits<std::uint32_t>::max();
    state.rightTrigger = 0x80000000u;

    state.lx = std::numeric_limits<std::int32_t>::min();
    state.ly = std::numeric_limits<std::int32_t>::min();
    state.rx = std::numeric_limits<std::int32_t>::max();
    state.ry = std::numeric_limits<std::int32_t>::max();

    const XboxGipInputPacket input =
        encoder.encodeInput(state, 7);

    XgipPacketView parsed {};
    assert(codec.parse(
        input.bytes.data(),
        input.bytes.size(),
        parsed
    ));

    assert(parsed.header.command ==
        static_cast<std::uint8_t>(XgipCommand::InputReport));
    assert(parsed.header.sequence == 7);
    assert(parsed.header.length == 32);
    assert(!parsed.header.internal);
    assert(parsed.payloadLength == 32);

    assert((input.bytes[4] & (1u << 1)) == 0); // Guide stays virtual-key only
    assert((input.bytes[4] & (1u << 2)) != 0); // Start
    assert((input.bytes[4] & (1u << 3)) != 0); // Back
    assert((input.bytes[4] & (1u << 4)) != 0); // A
    assert((input.bytes[4] & (1u << 5)) != 0); // B
    assert((input.bytes[4] & (1u << 6)) != 0); // X
    assert((input.bytes[4] & (1u << 7)) != 0); // Y

    assert((input.bytes[5] & (1u << 0)) != 0); // Up
    assert((input.bytes[5] & (1u << 3)) != 0); // Right
    assert((input.bytes[5] & (1u << 4)) != 0); // LB
    assert((input.bytes[5] & (1u << 5)) != 0); // RB
    assert((input.bytes[5] & (1u << 6)) != 0); // L3
    assert((input.bytes[5] & (1u << 7)) != 0); // R3

    assert(readLe16(&input.bytes[6]) == 0x03FF);
    assert(readLe16(&input.bytes[8]) >= 0x01FF);
    assert(readLe16(&input.bytes[8]) <= 0x0200);

    assert(
        static_cast<std::int16_t>(readLe16(&input.bytes[10])) ==
        std::numeric_limits<std::int16_t>::min()
    );
    assert(
        static_cast<std::int16_t>(readLe16(&input.bytes[12])) ==
        std::numeric_limits<std::int16_t>::max()
    );
    assert(
        static_cast<std::int16_t>(readLe16(&input.bytes[14])) ==
        std::numeric_limits<std::int16_t>::max()
    );
    assert(
        static_cast<std::int16_t>(readLe16(&input.bytes[16])) ==
        std::numeric_limits<std::int16_t>::min()
    );

    const XboxGipVirtualKeyPacket guideOn =
        encoder.encodeGuide(true, 9);

    assert(codec.parse(
        guideOn.bytes.data(),
        guideOn.bytes.size(),
        parsed
    ));
    assert(parsed.header.command ==
        static_cast<std::uint8_t>(XgipCommand::VirtualKeycode));
    assert(parsed.header.internal);
    assert(parsed.header.sequence == 9);
    assert(parsed.payloadLength == 2);
    assert(parsed.payload[0] == 0x01);
    assert(parsed.payload[1] == 0x5B);

    const XboxGipVirtualKeyPacket guideOff =
        encoder.encodeGuide(false, 10);
    assert(guideOff.bytes[4] == 0x00);
    assert(guideOff.bytes[5] == 0x5B);

    const XboxGipKeepAlivePacket keepAlive =
        encoder.encodeKeepAlive(3);
    assert(codec.parse(
        keepAlive.bytes.data(),
        keepAlive.bytes.size(),
        parsed
    ));
    assert(parsed.header.command ==
        static_cast<std::uint8_t>(XgipCommand::KeepAlive));
    assert(parsed.header.internal);
    assert(parsed.payloadLength == 4);
    assert(parsed.payload[0] == 0x80);

    return 0;
}
