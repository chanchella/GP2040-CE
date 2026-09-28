#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x28000000;
constexpr std::int32_t kLookAxisDeadzone = 0x01000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

// Game 1 calibration source:
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

// Mouse / right-stick look gesture.
//
// Touch coordinates are still in the phone's natural portrait axes while the
// game is landscape. Therefore:
//   mouse/right-stick RIGHT -> touch Y increases
//   mouse/right-stick DOWN  -> touch X decreases
//
// Firmware encodes touch-profile mouse deltas linearly into rx/ry with
// +/-48 mouse counts reaching full scale. These max steps correspond to about
// three physical screen pixels per mouse count on the 2388x1080 panel:
//   landscape horizontal: 41 HID units/count * 48 = 1968
//   landscape vertical:   91 HID units/count * 48 = 4368
//
// Contact 4 is isolated from movement/buttons (IDs 0..3), so mouse look can
// coexist with keyboard movement, Cross, Square and R2 as real multitouch.
constexpr std::uint8_t kLookContactId = 4;
constexpr std::uint16_t kLookCenterX = 16384;
constexpr std::uint16_t kLookCenterY = 16384;
constexpr std::uint16_t kLookMinX = 4000;
constexpr std::uint16_t kLookMaxX = 28767;
constexpr std::uint16_t kLookMinY = 5000;
constexpr std::uint16_t kLookMaxY = 27767;
constexpr std::int32_t kLookMaxHorizontalStep = 1968;
constexpr std::int32_t kLookMaxVerticalStep = 4368;

bool hasDpad(std::uint8_t dpad, DpadBits bit) {
    return
        (dpad & static_cast<std::uint8_t>(bit)) != 0;
}

std::int64_t abs64(std::int32_t value) {
    return value < 0
        ? -static_cast<std::int64_t>(value)
        : static_cast<std::int64_t>(value);
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
    return
        abs64(x) >= kLookAxisDeadzone ||
        abs64(y) >= kLookAxisDeadzone;
}

std::int32_t MobileTouchMapper::axisToStep(
    std::int32_t axis,
    std::int32_t maxStep
) {
    if (axis == 0 || maxStep <= 0) {
        return 0;
    }

    const std::int64_t numerator =
        static_cast<std::int64_t>(axis) *
        static_cast<std::int64_t>(maxStep);

    const std::int64_t denominator =
        axis < 0
            ? static_cast<std::int64_t>(
                std::numeric_limits<std::int32_t>::max()
            ) + 1
            : static_cast<std::int64_t>(
                std::numeric_limits<std::int32_t>::max()
            );

    return static_cast<std::int32_t>(
        numerator / denominator
    );
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

void MobileTouchMapper::resetLookTouch() {
    lookTouchActive_ = false;
    lookTouchX_ = kLookCenterX;
    lookTouchY_ = kLookCenterY;
}

MobileTouchFrame MobileTouchMapper::map(
    const LogicalGamepadState& state
) {
    MobileTouchFrame frame {};

    if (!state.connected) {
        resetLookTouch();
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

    // Mouse look / right-stick camera touch.
    //
    // This is a RELATIVE touchscreen drag, not an absolute cursor. Each input
    // report advances one persistent virtual finger by an amount proportional
    // to the mouse delta. When motion stops the contact disappears; the V4
    // output layer then emits the explicit Tip-Switch-clear UP report.
    //
    // If the virtual finger approaches an edge, omit it for one report. That
    // produces a clean UP; the next motion report re-anchors at center rather
    // than jumping across the screen while the finger is still down.
    if (axisActive(state.rx, state.ry)) {
        if (!lookTouchActive_) {
            lookTouchActive_ = true;
            lookTouchX_ = kLookCenterX;
            lookTouchY_ = kLookCenterY;
        }

        const std::int32_t horizontalStep =
            axisToStep(
                state.rx,
                kLookMaxHorizontalStep
            );

        const std::int32_t verticalStep =
            axisToStep(
                state.ry,
                kLookMaxVerticalStep
            );

        const std::int64_t candidateX =
            static_cast<std::int64_t>(lookTouchX_) -
            verticalStep;

        const std::int64_t candidateY =
            static_cast<std::int64_t>(lookTouchY_) +
            horizontalStep;

        if (
            candidateX < kLookMinX ||
            candidateX > kLookMaxX ||
            candidateY < kLookMinY ||
            candidateY > kLookMaxY
        ) {
            resetLookTouch();
        } else {
            lookTouchX_ = clampCoord(candidateX);
            lookTouchY_ = clampCoord(candidateY);

            append(
                frame,
                kLookContactId,
                lookTouchX_,
                lookTouchY_
            );
        }
    } else {
        resetLookTouch();
    }

    return frame;
}

} // namespace oag
