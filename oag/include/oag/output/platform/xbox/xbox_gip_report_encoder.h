#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct XboxGipInputPacket {
    static constexpr std::size_t kSize = 36;
    std::array<std::uint8_t, kSize> bytes {};
};

struct XboxGipVirtualKeyPacket {
    static constexpr std::size_t kSize = 6;
    std::array<std::uint8_t, kSize> bytes {};
};

struct XboxGipKeepAlivePacket {
    static constexpr std::size_t kSize = 8;
    std::array<std::uint8_t, kSize> bytes {};
};

class XboxGipReportEncoder {
public:
    XboxGipInputPacket encodeInput(
        const LogicalGamepadState& state,
        std::uint8_t sequence
    ) const;

    XboxGipVirtualKeyPacket encodeGuide(
        bool pressed,
        std::uint8_t sequence
    ) const;

    XboxGipKeepAlivePacket encodeKeepAlive(
        std::uint8_t sequence
    ) const;

private:
    static std::int16_t axisTo16(std::int32_t value);
    static std::int16_t yAxisTo16(std::int32_t value);
    static std::uint16_t triggerTo10(std::uint32_t value);

    static void writeLe16(
        std::uint8_t* destination,
        std::uint16_t value
    );
};

} // namespace oag
