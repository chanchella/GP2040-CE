#include "oag/output/platform/playstation/ps4_feedback_decoder.h"

namespace oag {

bool Ps4FeedbackDecoder::decodeOutput(
    const std::uint8_t* report,
    std::size_t length,
    Ps4Feedback& output
) const {
    if (report == nullptr || length < 11) {
        return false;
    }

    if (report[0] != 0x05) {
        return false;
    }

    const std::uint8_t flags = report[1];

    Ps4Feedback next {};
    next.rumble.leftMotor = report[5];
    next.rumble.rightMotor = report[4];
    next.rumble.generation = output.rumble.generation + 1u;

    next.ledUpdate = (flags & (1u << 1)) != 0;
    next.ledRed = report[6];
    next.ledGreen = report[7];
    next.ledBlue = report[8];
    next.blinkOn = report[9];
    next.blinkOff = report[10];
    next.generation = output.generation + 1u;

    output = next;
    return true;
}

} // namespace oag
