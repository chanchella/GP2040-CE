#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag {

// PUBG Profile-7 synthetic actions. These bits exist only inside the
// dedicated raw keyboard/mouse -> touch path.
inline constexpr std::uint64_t kPubgMouseLeftButton = 1ull << 63;
inline constexpr std::uint64_t kPubgTriangleShortButton = 1ull << 62;
inline constexpr std::uint64_t kPubgTriangleHoldButton = 1ull << 61;
inline constexpr std::uint64_t kPubgMouseLookHoldButton = 1ull << 60;
inline constexpr std::uint64_t kPubgShiftButton = 1ull << 59;
inline constexpr std::uint64_t kPubgMouseRightButton = 1ull << 58;
inline constexpr std::uint64_t kPubgKeyRButton = 1ull << 57;
inline constexpr std::uint64_t kPubgScrollDownButton = 1ull << 56;
inline constexpr std::uint64_t kPubgScrollUpButton = 1ull << 55;
inline constexpr std::uint64_t kPubgMouseMiddleButton = 1ull << 54;
inline constexpr std::uint64_t kPubgKeyGButton = 1ull << 53;

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
    void resetMovementTouch();

    bool lookTouchActive_ = false;
    std::uint16_t lookTouchX_ = 16384;
    std::uint16_t lookTouchY_ = 16384;

    bool movementTouchActive_ = false;
};

} // namespace oag
