#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/platform/playstation/ps4_feedback_decoder.h"
#include "oag/output/platform/playstation/ps4_report_encoder.h"

using namespace oag;

int main() {
    Ps4ReportEncoder encoder;

    LogicalGamepadState neutral {};
    neutral.connected = true;

    const Ps4InputReport n = encoder.encode(neutral, 0);

    assert(n.bytes.size() == 64);
    assert(n.bytes[0] == 0x01);
    assert(n.bytes[1] == 0x7F || n.bytes[1] == 0x80);
    assert(n.bytes[2] == 0x7F || n.bytes[2] == 0x80);
    assert((n.bytes[5] & 0x0Fu) == 0x0F);

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

    const Ps4InputReport r = encoder.encode(state, 0x2A);

    assert(r.bytes[1] == 0x00);
    assert(r.bytes[2] == 0x00);
    assert(r.bytes[3] == 0xFF);
    assert(r.bytes[4] == 0xFF);

    assert((r.bytes[5] & 0x0Fu) == 0x01); // up-right
    assert((r.bytes[5] & (1u << 4)) != 0); // Square
    assert((r.bytes[5] & (1u << 5)) != 0); // Cross
    assert((r.bytes[5] & (1u << 6)) != 0); // Circle
    assert((r.bytes[5] & (1u << 7)) != 0); // Triangle

    assert((r.bytes[6] & 0xFFu) == 0xFFu);
    assert((r.bytes[7] & 0x01u) != 0); // PS
    assert((r.bytes[7] >> 2u) == 0x2A);

    assert(r.bytes[8] == 0xFF);
    assert(r.bytes[9] == 0x80);

    Ps4FeedbackDecoder feedbackDecoder;
    Ps4Feedback feedback {};

    std::uint8_t output[32] {};
    output[0] = 0x05;
    output[1] = 0x03; // rumble + LED enabled
    output[4] = 0x51;
    output[5] = 0xD2;
    output[6] = 0x11;
    output[7] = 0x22;
    output[8] = 0x33;
    output[9] = 0x04;
    output[10] = 0x05;

    assert(feedbackDecoder.decodeOutput(
        output,
        sizeof(output),
        feedback
    ));

    assert(feedback.rumble.leftMotor == 0xD2);
    assert(feedback.rumble.rightMotor == 0x51);
    assert(feedback.ledUpdate);
    assert(feedback.ledRed == 0x11);
    assert(feedback.ledGreen == 0x22);
    assert(feedback.ledBlue == 0x33);
    assert(feedback.blinkOn == 0x04);
    assert(feedback.blinkOff == 0x05);

    return 0;
}
