#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

// PUBG Profile-7 synthetic actions. These bits are injected only by the
// dedicated PUBG keyboard/mouse path in firmware.
inline constexpr std::uint64_t kPubgMouseLeftButton = 1ull << 63;
inline constexpr std::uint64_t kPubgTriangleShortButton = 1ull << 62;
inline constexpr std::uint64_t kPubgTriangleHoldButton = 1ull << 61;

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

    void resetLookTouch();

    bool lookTouchActive_ = false;
    std::uint16_t lookTouchX_ = 11302;
    std::uint16_t lookTouchY_ = 26072;
};

} // namespace oag
