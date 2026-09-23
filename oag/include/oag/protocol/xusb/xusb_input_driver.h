#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/device/device_id.h"
#include "oag/input/gamepad_state.h"

namespace oag {

class XusbInputDriver {
public:
    bool parse(
        DeviceId source,
        const std::uint8_t* report,
        std::size_t length,
        std::uint64_t timestampUs,
        UniversalGamepadState& output
    ) const;

private:
    static std::int32_t normalizeAxis(std::int16_t value);
    static std::int32_t normalizeYAxis(std::int16_t value);
    static std::uint32_t normalizeTrigger(std::uint8_t value);
};

} // namespace oag
