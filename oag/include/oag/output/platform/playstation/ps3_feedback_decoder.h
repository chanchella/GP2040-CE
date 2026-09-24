#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/feedback/rumble_command.h"

namespace oag {

struct Ps3Feedback {
    RumbleCommand rumble {};
    std::uint8_t playerLedMask = 0;
    std::uint32_t generation = 0;
};

class Ps3FeedbackDecoder {
public:
    bool decodeOutput(
        const std::uint8_t* report,
        std::size_t length,
        Ps3Feedback& output
    ) const;
};

} // namespace oag
