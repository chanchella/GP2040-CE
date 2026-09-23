#include "oag/output/xinput/xinput_feedback_decoder.h"

namespace oag {

bool XinputFeedbackDecoder::decodeRumble(
    const std::uint8_t* report,
    std::size_t length,
    RumbleCommand& output
) const {
    if (report == nullptr || length < 5) {
        return false;
    }

    // Xbox 360/XInput rumble OUT packet.
    // byte 0: command 0x00
    // byte 1: packet length 0x08
    // byte 3: large/left motor
    // byte 4: small/right motor
    if (report[0] != 0x00 || report[1] != 0x08) {
        return false;
    }

    RumbleCommand next {};
    next.leftMotor = report[3];
    next.rightMotor = report[4];
    next.generation = output.generation + 1u;

    output = next;
    return true;
}

} // namespace oag
