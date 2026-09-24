#pragma once

#include <cstdint>

namespace oag {

struct TouchDigitizerState {
    bool connected = false;
    bool inRange = false;
    bool contact = false;
    std::uint8_t contactId = 0;
    std::uint16_t x = 16384;
    std::uint16_t y = 16384;
    std::uint32_t generation = 0;
    std::uint64_t timestampUs = 0;
};

struct PenDigitizerState {
    bool connected = false;
    bool inRange = false;
    bool tip = false;
    bool barrel = false;
    bool eraser = false;
    std::uint16_t x = 16384;
    std::uint16_t y = 16384;
    std::uint16_t pressure = 0;
    std::int16_t tiltX = 0;
    std::int16_t tiltY = 0;
    std::uint32_t generation = 0;
    std::uint64_t timestampUs = 0;
};

} // namespace oag
