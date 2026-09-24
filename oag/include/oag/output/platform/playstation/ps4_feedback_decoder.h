#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/feedback/rumble_command.h"

namespace oag {

struct Ps4Feedback {
    RumbleCommand rumble {};
    bool ledUpdate = false;
    std::uint8_t ledRed = 0;
    std::uint8_t ledGreen = 0;
    std::uint8_t ledBlue = 0;
    std::uint8_t blinkOn = 0;
    std::uint8_t blinkOff = 0;
    std::uint32_t generation = 0;
};

class Ps4FeedbackDecoder {
public:
    bool decodeOutput(
        const std::uint8_t* report,
        std::size_t length,
        Ps4Feedback& output
    ) const;
};

} // namespace oag
