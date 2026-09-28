#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x28000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

// Game 1 V3 calibration source:
// Android Pointer Location measurements on the real phone while landscape.
// Android reports this device in natural portrait coordinates: 1080x2388.
//
// Empirical proof:
//   diagnostic HID center (16384,16384)
//   -> Android Pointer Location X=540, Y=1194 exactly.
//
// Therefore game targets are normalized directly from the measured
// natural-orientation Android coordinates:
//   HID_X = round(pointer_x * 32768 / 1080)
//   HID_Y = round(pointer_y * 32768 / 2388)
//
// User-measured targets:
//   Cross  pointer (384.5,2278) -> HID (11666,31259)
//   Square pointer (210,2080)   -> HID (6372,28542)
//   R2     pointer (455,2070)   -> HID (13805,28404)
//   Left   pointer (224,257)    -> HID (6796,3527)
//   Right  pointer (209,682)    -> HID (6341,9358)
//   R3     pointer (218,455)    -> HID (6614,6243)
constexpr std::uint16_t kMoveLeftX = 6796;
constexpr std::uint16_t kMoveLeftY = 3527;
constexpr std::uint16_t kR3X = 6614;
constexpr std::uint16_t kR3Y = 6243;
constexpr std::uint16_t kMoveRightX = 6341;
constexpr std::uint16_t kMoveRightY = 9358;

constexpr std::uint16_t kR2X = 13805;
constexpr std::uint16_t kR2Y = 28404;
constexpr std::uint16_t kCrossX = 11666;
constexpr std::uint16_t kCrossY = 31259;
constexpr std::uint16_t kSquareX = 6372;
constexpr std::uint16_t kSquareY = 28542;

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
    // Game 1 uses fixed touchscreen targets for the left control rather than
    // a free analog touch joystick.
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

    // Contact 0 owns the whole left-side control. R3 wins over movement so
    // there is never more than one virtual finger on that same UI control.
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

    // PlayStation Cross / logical South.
    if (state.buttons & ButtonSouth) {
        append(frame, 1, kCrossX, kCrossY);
    }

    // PlayStation Square / logical West.
    if (state.buttons & ButtonWest) {
        append(frame, 2, kSquareX, kSquareY);
    }

    // Physical/logical R2. Existing keyboard/mouse infrastructure maps
    // mouse-left to RightTrigger, so mouse-left reaches this target too.
    if (state.rightTrigger > kTriggerThreshold) {
        append(frame, 3, kR2X, kR2Y);
    }

    return frame;
}

} // namespace oag
