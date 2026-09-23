#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/feedback/rumble_command.h"

namespace oag {

class XinputFeedbackDecoder {
public:
    bool decodeRumble(
        const std::uint8_t* report,
        std::size_t length,
        RumbleCommand& output
    ) const;
};

} // namespace oag
