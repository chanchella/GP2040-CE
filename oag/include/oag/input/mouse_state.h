#pragma once

#include <cstdint>

namespace oag {

enum MouseButton : std::uint16_t {
    MouseButtonLeft    = 1u << 0,
    MouseButtonRight   = 1u << 1,
    MouseButtonMiddle  = 1u << 2,
    MouseButtonBack    = 1u << 3,
    MouseButtonForward = 1u << 4,
};

struct MouseMotion {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

struct MouseState {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::int16_t wheel = 0;
    std::int16_t pan = 0;
    std::uint32_t generation = 0;
    std::uint64_t timestampUs = 0;
};

} // namespace oag
