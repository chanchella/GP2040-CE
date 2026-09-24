#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/device/device_id.h"
#include "oag/input/gamepad_state.h"

namespace oag {

class XgipInputDriver {
public:
    bool parse(
        DeviceId source,
        const std::uint8_t* report,
        std::size_t length,
        std::uint64_t timestampUs,
        UniversalGamepadState& output
    ) const;

private:
    static std::int32_t expandAxis(std::int16_t value);
    static std::int32_t expandYAxis(std::int16_t value);
    static std::uint32_t expandTrigger10(std::uint16_t value);
};

} // namespace oag
