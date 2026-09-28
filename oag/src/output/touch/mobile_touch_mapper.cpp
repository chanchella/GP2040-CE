#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x28000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

// Game 1 calibration source:
// supplied screenshot = 1910x864
// target phone landscape = 2388x1080
// HID touchscreen logical coordinates = 0..32767.
//
// Final user-confirmed labels:
//   left arrow  -> movement Left
//   center      -> R3 click
//   right arrow -> movement Right
//   upper R2    -> physical/logical R2
//   upper-right X mark -> PlayStation Cross / X
//   large lower-right blue square -> PlayStation Square
//
// Screenshot centers -> HID normalized:
//   Left   (183,683)  -> (3139,25903)
//   R3     (356,681)  -> (6107,25827)
//   Right  (506,684)  -> (8681,25941)
//   R2     (1640,485) -> (28135,18394)
//   Cross  (1801,554) -> (30897,21010)
//   Square (1648,711) -> (28272,26965)
constexpr std::uint16_t kMoveLeftX = 3139;
constexpr std::uint16_t kMoveLeftY = 25903;
constexpr std::uint16_t kR3X = 6107;
constexpr std::uint16_t kR3Y = 25827;
constexpr std::uint16_t kMoveRightX = 8681;
constexpr std::uint16_t kMoveRightY = 25941;

constexpr std::uint16_t kR2X = 28135;
constexpr std::uint16_t kR2Y = 18394;
constexpr std::uint16_t kCrossX = 30897;
constexpr std::uint16_t kCrossY = 21010;
constexpr std::uint16_t kSquareX = 28272;
constexpr std::uint16_t kSquareY = 26965;

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
    // Game 1 has a fixed side-scroller rocker, not a free analog joystick.
    // Keep the generic class contract intact while this profile maps only
    // explicit left/right touch targets.
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

    // Contact 0 owns the left rocker. R3 has priority over movement so we
    // never place two fingers on the same three-part control.
    if (state.buttons & ButtonRightStick) {
        append(frame, 0, kR3X, kR3Y);
    } else {
        const bool left =
            hasDpad(state.dpad, DpadBits::Left) ||
            state.lx <= -kStickDeadzone;

        const bool right =
            hasDpad(state.dpad, DpadBits::Right) ||
            state.lx >= kStickDeadzone;

        if (left != right) {
            if (left) {
                append(frame, 0, kMoveLeftX, kMoveLeftY);
            } else {
                append(frame, 0, kMoveRightX, kMoveRightY);
            }
        }
    }

    // PlayStation Cross / logical South -> the user-marked X button.
    if (state.buttons & ButtonSouth) {
        append(frame, 1, kCrossX, kCrossY);
    }

    // PlayStation Square / logical West -> the user-marked blue-square button.
    if (state.buttons & ButtonWest) {
        append(frame, 2, kSquareX, kSquareY);
    }

    // Physical/logical R2 -> the user-marked R2 button.
    // The existing keyboard/mouse profile maps mouse-left to Right Trigger,
    // so mouse-left reaches this exact touch target as well.
    if (state.rightTrigger > kTriggerThreshold) {
        append(frame, 3, kR2X, kR2Y);
    }

    return frame;
}

} // namespace oag
