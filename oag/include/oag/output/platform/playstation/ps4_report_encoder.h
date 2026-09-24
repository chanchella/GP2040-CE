#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct Ps4InputReport {
    static constexpr std::size_t kSize = 64;
    std::array<std::uint8_t, kSize> bytes {};
};

class Ps4ReportEncoder {
public:
    Ps4InputReport encode(
        const LogicalGamepadState& state,
        std::uint8_t reportCounter
    ) const;

private:
    static std::uint8_t axisTo8(std::int32_t value);
    static std::uint8_t triggerTo8(std::uint32_t value);
    static std::uint8_t dpadHat(std::uint8_t dpad);
};

} // namespace oag
