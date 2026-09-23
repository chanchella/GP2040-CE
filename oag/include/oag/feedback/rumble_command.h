#pragma once

#include <cstdint>

namespace oag {

struct RumbleCommand {
    std::uint8_t leftMotor = 0;
    std::uint8_t rightMotor = 0;
    std::uint32_t generation = 0;

    constexpr bool active() const {
        return leftMotor != 0 || rightMotor != 0;
    }
};

} // namespace oag
