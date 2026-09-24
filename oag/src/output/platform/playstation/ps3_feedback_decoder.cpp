#include "oag/output/platform/playstation/ps3_feedback_decoder.h"

namespace oag {

bool Ps3FeedbackDecoder::decodeOutput(
    const std::uint8_t* report,
    std::size_t length,
    Ps3Feedback& output
) const {
    if (report == nullptr || length < 10) {
        return false;
    }

    Ps3Feedback next {};
    next.rumble.leftMotor = report[4];
    next.rumble.rightMotor = report[2];
    next.rumble.generation = output.rumble.generation + 1u;
    next.playerLedMask = static_cast<std::uint8_t>(report[9] & 0x0Fu);
    next.generation = output.generation + 1u;

    output = next;
    return true;
}

} // namespace oag
