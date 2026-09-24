#pragma once

#include <cstdint>

#include "oag/device/device_id.h"

namespace oag {

enum class DpadBits : std::uint8_t {
    None  = 0,
    Up    = 1u << 0,
    Down  = 1u << 1,
    Left  = 1u << 2,
    Right = 1u << 3,
};

enum GamepadButton : std::uint64_t {
    ButtonSouth       = 1ull << 0,
    ButtonEast        = 1ull << 1,
    ButtonWest        = 1ull << 2,
    ButtonNorth       = 1ull << 3,
    ButtonLeftBumper  = 1ull << 4,
    ButtonRightBumper = 1ull << 5,
    ButtonLeftStick   = 1ull << 6,
    ButtonRightStick  = 1ull << 7,
    ButtonBack        = 1ull << 8,
    ButtonStart       = 1ull << 9,
    ButtonGuide       = 1ull << 10,
    // Dedicated Share/Capture semantic when a transport exposes it.
    // Legacy View/Select/Share controls remain represented by ButtonBack.
    ButtonShare       = 1ull << 11,
};

struct UniversalGamepadState {
    DeviceId source {};
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
