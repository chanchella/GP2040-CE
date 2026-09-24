#pragma once

#include <array>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct SwitchProInputReport {
    static constexpr std::size_t kSize = 64;

    std::array<std::uint8_t, kSize> bytes {};
};

class SwitchProReportEncoder {
public:
    SwitchProInputReport encode(
        const LogicalGamepadState& state,
        std::uint8_t timestamp
    ) const;

private:
    static std::uint16_t axisTo12(std::int32_t value);
    static void packStick(
        std::uint8_t* destination,
        std::uint16_t x,
        std::uint16_t y
    );
};

} // namespace oag
