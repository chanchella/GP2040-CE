#pragma once

#include <cstdint>

namespace oag {

struct LogicalGamepadState {
    bool connected = false;
    std::uint64_t buttons = 0;
    std::uint8_t dpad = 0;

    std::int32_t lx = 0;
    std::int32_t ly = 0;
    std::int32_t rx = 0;
    std::int32_t ry = 0;

    std::uint32_t leftTrigger = 0;
    std::uint32_t rightTrigger = 0;

    std::uint32_t generation = 0;
    std::uint64_t timestampUs = 0;
};

} // namespace oag
