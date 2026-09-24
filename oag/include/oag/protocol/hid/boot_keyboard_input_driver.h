#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/device/device_id.h"
#include "oag/input/keyboard_state.h"

namespace oag {

class BootKeyboardInputDriver {
public:
    bool parse(
        DeviceId source,
        const std::uint8_t* report,
        std::size_t length,
        std::uint64_t timestampUs,
        KeyboardState& output
    ) const;
};

} // namespace oag
