#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/platform/switch/switch_pro_report_encoder.h"

using namespace oag;

namespace {

std::uint16_t unpackX(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        p[0] |
        ((p[1] & 0x0Fu) << 8u)
    );
}

std::uint16_t unpackY(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        (p[1] >> 4u) |
        (static_cast<std::uint16_t>(p[2]) << 4u)
    );
}

} // namespace

int main() {
    SwitchProReportEncoder encoder;

    LogicalGamepadState neutral {};
    neutral.connected = true;

    const SwitchProInputReport n =
        encoder.encode(neutral, 0x44);

    assert(n.bytes[0] == 0x30);
    assert(n.bytes[1] == 0x44);
    assert(n.bytes[2] == 0x80);
    assert(n.bytes[4] == 0x80);
    assert(n.bytes[12] == 0x09);

    const std::uint16_t neutralLx = unpackX(&n.bytes[6]);
    const std::uint16_t neutralLy = unpackY(&n.bytes[6]);

    assert(neutralLx >= 0x07FE && neutralLx <= 0x0800);
    assert(neutralLy >= 0x07FF && neutralLy <= 0x0801);

    LogicalGamepadState state {};
    state.connected = true;
    state.buttons =
        ButtonSouth |
        ButtonEast |
        ButtonWest |
        ButtonNorth |
        ButtonLeftBumper |
        ButtonRightBumper |
        ButtonBack |
        ButtonStart |
        ButtonLeftStick |
        ButtonRightStick |
        ButtonGuide;

    state.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Right);

    state.leftTrigger = std::numeric_limits<std::uint32_t>::max();
    state.rightTrigger = std::numeric_limits<std::uint32_t>::max();

    state.lx = std::numeric_limits<std::int32_t>::min();
    state.ly = std::numeric_limits<std::int32_t>::min();
    state.rx = std::numeric_limits<std::int32_t>::max();
    state.ry = std::numeric_limits<std::int32_t>::max();

    const SwitchProInputReport r =
        encoder.encode(state, 0x55);

    assert((r.bytes[3] & (1u << 0)) != 0); // Y
    assert((r.bytes[3] & (1u << 1)) != 0); // X
    assert((r.bytes[3] & (1u << 2)) != 0); // B
    assert((r.bytes[3] & (1u << 3)) != 0); // A
    assert((r.bytes[3] & (1u << 6)) != 0); // R
    assert((r.bytes[3] & (1u << 7)) != 0); // ZR

    assert((r.bytes[4] & (1u << 0)) != 0); // Minus
    assert((r.bytes[4] & (1u << 1)) != 0); // Plus
    assert((r.bytes[4] & (1u << 2)) != 0); // R3
    assert((r.bytes[4] & (1u << 3)) != 0); // L3
    assert((r.bytes[4] & (1u << 4)) != 0); // Home

    assert((r.bytes[5] & (1u << 1)) != 0); // Up
    assert((r.bytes[5] & (1u << 2)) != 0); // Right
    assert((r.bytes[5] & (1u << 6)) != 0); // L
    assert((r.bytes[5] & (1u << 7)) != 0); // ZL

    assert(unpackX(&r.bytes[6]) == 0x0000);
    assert(unpackY(&r.bytes[6]) == 0x0FFF);
    assert(unpackX(&r.bytes[9]) == 0x0FFF);
    assert(unpackY(&r.bytes[9]) == 0x0000);

    return 0;
}
