#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class PcHidOutput {
public:
    static constexpr std::size_t kOutputSlots = 4;

    void task();

    bool send(
        std::uint8_t logicalSlot,
        const LogicalGamepadState& state
    );

    bool sendNeutral(std::uint8_t logicalSlot);

private:
    struct __attribute__((packed)) Report {
        // Keep the first four HID axes as the two sticks so Chrome/Web
        // Gamepad raw mappings see axes[0..3] in the expected order.
        std::int8_t lx = 0;
        std::int8_t ly = 0;
        std::int8_t rx = 0;
        std::int8_t ry = 0;

        // Analog triggers remain native HID axes for Android/native games.
        std::uint8_t leftTrigger = 0;
        std::uint8_t rightTrigger = 0;

        // Hat remains available for native HID consumers.
        std::uint8_t hat = 8;

        // 18 logical buttons + 6 constant padding bits.
        // Web-friendly indices:
        // 0 A/South, 1 B/East, 2 X/West, 3 Y/North,
        // 4 LB, 5 RB, 6 LT, 7 RT, 8 Back, 9 Start,
        // 10 L3, 11 R3, 12 Up, 13 Down, 14 Left, 15 Right,
        // 16 Guide/Home, 17 Share/Capture.
        std::uint8_t buttons0To7 = 0;
        std::uint8_t buttons8To15 = 0;
        std::uint8_t buttons16To17 = 0;
    };

    static_assert(sizeof(Report) == 10);

    bool flush(std::size_t slot);

    std::array<Report, kOutputSlots> reports_ {};
    std::array<bool, kOutputSlots> pending_ {};
};

} // namespace oag::firmware
