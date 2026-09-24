#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/platform/playstation/ps3_feedback_decoder.h"
#include "oag/output/platform/playstation/ps3_report_encoder.h"

using namespace oag;

int main() {
    Ps3ReportEncoder encoder;

    LogicalGamepadState neutral {};
    neutral.connected = true;

    const Ps3Ds3Report n = encoder.encode(neutral);

    assert(n.bytes.size() == 51);
    assert(n.bytes[0] == 0x01);
    assert(n.bytes[6] == 0x7F || n.bytes[6] == 0x80);
    assert(n.bytes[7] == 0x7F || n.bytes[7] == 0x80);
    assert(n.bytes[29] == 0x02);
    assert(n.bytes[30] == 0x05);
    assert(n.bytes[31] == 0x10);

    assert(n.bytes[41] == 0x01);
    assert(n.bytes[42] == 0xFF);

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
    state.rightTrigger = 0x80808080u;

    state.lx = std::numeric_limits<std::int32_t>::min();
    state.ly = std::numeric_limits<std::int32_t>::min();
    state.rx = std::numeric_limits<std::int32_t>::max();
    state.ry = std::numeric_limits<std::int32_t>::max();

    const Ps3Ds3Report r = encoder.encode(state);

    assert((r.bytes[2] & (1u << 0)) != 0); // Select
    assert((r.bytes[2] & (1u << 1)) != 0); // L3
    assert((r.bytes[2] & (1u << 2)) != 0); // R3
    assert((r.bytes[2] & (1u << 3)) != 0); // Start
    assert((r.bytes[2] & (1u << 4)) != 0); // Up
    assert((r.bytes[2] & (1u << 5)) != 0); // Right

    assert((r.bytes[3] & (1u << 0)) != 0); // L2
    assert((r.bytes[3] & (1u << 1)) != 0); // R2
    assert((r.bytes[3] & (1u << 2)) != 0); // L1
    assert((r.bytes[3] & (1u << 3)) != 0); // R1
    assert((r.bytes[3] & (1u << 4)) != 0); // Triangle
    assert((r.bytes[3] & (1u << 5)) != 0); // Circle
    assert((r.bytes[3] & (1u << 6)) != 0); // Cross
    assert((r.bytes[3] & (1u << 7)) != 0); // Square

    assert((r.bytes[4] & 0x01u) != 0); // PS

    assert(r.bytes[6] == 0x00);
    assert(r.bytes[7] == 0x00);
    assert(r.bytes[8] == 0xFF);
    assert(r.bytes[9] == 0xFF);

    assert(r.bytes[14] == 0xFF);
    assert(r.bytes[15] == 0xFF);
    assert(r.bytes[16] == 0x00);
    assert(r.bytes[17] == 0x00);

    assert(r.bytes[18] == 0xFF);
    assert(r.bytes[19] == 0x80);
    assert(r.bytes[20] == 0xFF);
    assert(r.bytes[21] == 0xFF);

    assert(r.bytes[22] == 0xFF);
    assert(r.bytes[23] == 0xFF);
    assert(r.bytes[24] == 0xFF);
    assert(r.bytes[25] == 0xFF);

    Ps3FeedbackDecoder feedbackDecoder;
    Ps3Feedback feedback {};

    std::uint8_t outReport[48] {};
    outReport[2] = 0x7A; // right/small motor
    outReport[4] = 0xE1; // left/large motor
    outReport[9] = 0x05; // LED mask

    assert(feedbackDecoder.decodeOutput(
        outReport,
        sizeof(outReport),
        feedback
    ));

    assert(feedback.rumble.leftMotor == 0xE1);
    assert(feedback.rumble.rightMotor == 0x7A);
    assert(feedback.playerLedMask == 0x05);
    assert(feedback.generation == 1);

    return 0;
}
