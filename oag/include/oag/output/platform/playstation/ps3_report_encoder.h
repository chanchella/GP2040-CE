#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

// Matches the packed semantic PS3Report layout used by the Golden baseline.
// Live USB descriptor activation is intentionally a later hardware gate.
struct Ps3Ds3Report {
    static constexpr std::size_t kSize = 51;
    std::array<std::uint8_t, kSize> bytes {};
};

class Ps3ReportEncoder {
public:
    Ps3Ds3Report encode(const LogicalGamepadState& state) const;

private:
    static std::uint8_t axisTo8(std::int32_t value);
    static std::uint8_t triggerTo8(std::uint32_t value);
    static void writeLe16(
        std::array<std::uint8_t, Ps3Ds3Report::kSize>& bytes,
        std::size_t offset,
        std::uint16_t value
    );
};

} // namespace oag
