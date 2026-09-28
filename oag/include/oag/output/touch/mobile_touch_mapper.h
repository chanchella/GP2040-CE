#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct MobileTouchContact {
    std::uint8_t id = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct MobileTouchFrame {
    static constexpr std::size_t kMaxContacts = 10;

    std::array<MobileTouchContact, kMaxContacts> contacts {};
    std::uint8_t count = 0;
};

class MobileTouchMapper {
public:
    MobileTouchFrame map(const LogicalGamepadState& state);

private:
    static constexpr std::uint16_t kCoordMax = 32767;

    static std::uint16_t clampCoord(std::int64_t value);

    static bool axisActive(std::int32_t x, std::int32_t y);

    static std::int32_t axisToStep(
        std::int32_t axis,
        std::int32_t maxStep
    );

    static void append(
        MobileTouchFrame& frame,
        std::uint8_t id,
        std::uint16_t x,
        std::uint16_t y
    );

    static void appendStick(
        MobileTouchFrame& frame,
        std::uint8_t id,
        std::int32_t x,
        std::int32_t y,
        std::uint16_t centerX,
        std::uint16_t centerY,
        std::uint16_t radius
    );

    void resetLookTouch();

    bool lookTouchActive_ = false;
    std::uint16_t lookTouchX_ = 16384;
    std::uint16_t lookTouchY_ = 16384;
};

} // namespace oag
