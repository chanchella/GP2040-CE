#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x28000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

// Game 1 V2 calibration source:
// supplied/approved screenshot = 1910x864
// target phone landscape = 2388x1080
// HID touchscreen logical coordinates = 0..32767.
//
// User-approved targets:
//   left arrow  -> movement Left
//   center      -> R3 click
//   right arrow -> movement Right
//   upper R2    -> physical/logical R2
//   upper-right X mark -> PlayStation Cross / X
//   lower-right blue square region -> PlayStation Square
//
// Screenshot centers -> phone pixels -> HID normalized:
//   Left   (176,656)  -> (220,820)  -> (3020,24902)
//   R3     (339,654)  -> (424,818)  -> (5820,24841)
//   Right  (502,656)  -> (628,820)  -> (8621,24902)
//   R2     (1600,468) -> (2000,585) -> (27455,17765)
//   Cross  (1803,555) -> (2254,694) -> (30941,21075)
//   Square (1671,718) -> (2089,898) -> (28676,27270)
constexpr std::uint16_t kMoveLeftX = 3020;
constexpr std::uint16_t kMoveLeftY = 24902;
constexpr std::uint16_t kR3X = 5820;
constexpr std::uint16_t kR3Y = 24841;
constexpr std::uint16_t kMoveRightX = 8621;
constexpr std::uint16_t kMoveRightY = 24902;

constexpr std::uint16_t kR2X = 27455;
constexpr std::uint16_t kR2Y = 17765;
constexpr std::uint16_t kCrossX = 30941;
constexpr std::uint16_t kCrossY = 21075;
constexpr std::uint16_t kSquareX = 28676;
constexpr std::uint16_t kSquareY = 27270;

bool hasDpad(std::uint8_t dpad, DpadBits bit) {
    return
        (dpad & static_cast<std::uint8_t>(bit)) != 0;
}

} // namespace

std::uint16_t MobileTouchMapper::clampCoord(std::int64_t value) {
    return static_cast<std::uint16_t>(
        std::clamp<std::int64_t>(value, 0, kCoordMax)
    );
}

bool MobileTouchMapper::axisActive(
    std::int32_t x,
    std::int32_t y
) {
    const auto abs64 = [](std::int32_t v) -> std::int64_t {
        return v < 0
            ? -static_cast<std::int64_t>(v)
            : static_cast<std::int64_t>(v);
    };

    return
        abs64(x) >= kStickDeadzone ||
        abs64(y) >= kStickDeadzone;
}

void MobileTouchMapper::append(
    MobileTouchFrame& frame,
    std::uint8_t id,
    std::uint16_t x,
    std::uint16_t y
) {
    if (frame.count >= frame.contacts.size()) {
        return;
    }

    MobileTouchContact& contact =
        frame.contacts[frame.count++];

    contact.id = id;
    contact.x = x;
    contact.y = y;
}

void MobileTouchMapper::appendStick(
    MobileTouchFrame& frame,
    std::uint8_t id,
    std::int32_t x,
    std::int32_t y,
    std::uint16_t centerX,
    std::uint16_t centerY,
    std::uint16_t radius
) {
    // This side-scroller uses fixed left/right targets rather than a free
    // analog joystick. Keep the generic class contract intact while this
    // game profile maps explicit screen targets only.
    (void)frame;
    (void)id;
    (void)x;
    (void)y;
    (void)centerX;
    (void)centerY;
    (void)radius;
}

MobileTouchFrame MobileTouchMapper::map(
    const LogicalGamepadState& state
) const {
    MobileTouchFrame frame {};

    if (!state.connected) {
        return frame;
    }

    // Contact 0 owns the complete left rocker. R3 wins over movement so the
    // mapper never places two virtual fingers on the same physical control.
    if (state.buttons & ButtonRightStick) {
        append(frame, 0, kR3X, kR3Y);
    } else {
        const bool left =
            hasDpad(state.dpad, DpadBits::Left) ||
            state.lx <= -kStickDeadzone;

        const bool right =
            hasDpad(state.dpad, DpadBits::Right) ||
            state.lx >= kStickDeadzone;

        // Opposing directions cancel instead of creating an ambiguous touch.
        if (left != right) {
            if (left) {
                append(frame, 0, kMoveLeftX, kMoveLeftY);
            } else {
                append(frame, 0, kMoveRightX, kMoveRightY);
            }
        }
    }

    // PlayStation Cross / logical South.
    if (state.buttons & ButtonSouth) {
        append(frame, 1, kCrossX, kCrossY);
    }

    // PlayStation Square / logical West.
    if (state.buttons & ButtonWest) {
        append(frame, 2, kSquareX, kSquareY);
    }

    // Physical/logical R2. The existing keyboard/mouse infrastructure maps
    // mouse-left to RightTrigger, so it reaches the same touch target without
    // changing the proven keyboard/mouse mapper.
    if (state.rightTrigger > kTriggerThreshold) {
        append(frame, 3, kR2X, kR2Y);
    }

    return frame;
}

} // namespace oag
