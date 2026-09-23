#pragma once

#include <cstdint>

namespace oag {

struct __attribute__((packed)) XinputReport {
    std::uint8_t reportId = 0x00;
    std::uint8_t reportSize = 0x14;
    std::uint8_t buttons1 = 0;
    std::uint8_t buttons2 = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int16_t rx = 0;
    std::int16_t ry = 0;
    std::uint8_t reserved[6] {};
};

static_assert(sizeof(XinputReport) == 20);

} // namespace oag
