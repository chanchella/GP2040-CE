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

// Android Pointer Location reports this phone in its natural portrait space
// (1080x2388), even while the game is landscape.
//
// Conversion used for every measured target:
//   HID_X = round(pointer_x * 32768 / 1080)
//   HID_Y = round(pointer_y * 32768 / 2388)
//
// Previously hardware-verified Game1 controls retained unless explicitly
// replaced by the new measurements below.
constexpr std::uint16_t kMoveLeftX = 6796;
constexpr std::uint16_t kMoveLeftY = 3527;
constexpr std::uint16_t kR3X = 6614;
constexpr std::uint16_t kR3Y = 6243;
constexpr std::uint16_t kMoveRightX = 6341;
constexpr std::uint16_t kMoveRightY = 9358;

constexpr std::uint16_t kR2X = 13805;
constexpr std::uint16_t kR2Y = 28404;
constexpr std::uint16_t kCrossX = 10407;   // Pointer (343,2230)
constexpr std::uint16_t kCrossY = 30600;
constexpr std::uint16_t kSquareX = 6372;
constexpr std::uint16_t kSquareY = 28542;

// New measured controls.
constexpr std::uint16_t kMouseLeftX = 16869; // Pointer (556,2292)
constexpr std::uint16_t kMouseLeftY = 31451;

constexpr std::uint16_t kTriangleShortX = 2124; // Pointer (70,2016)
constexpr std::uint16_t kTriangleShortY = 27663;
constexpr std::uint16_t kTriangleHoldX = 2427;  // Pointer (80,2144)
constexpr std::uint16_t kTriangleHoldY = 29420;

constexpr std::uint16_t kShareX = 3034; // Pointer (100,215)
constexpr std::uint16_t kShareY = 2950;
constexpr std::uint16_t kR1X = 20935;   // Pointer (690,510)
constexpr std::uint16_t kR1Y = 6998;
constexpr std::uint16_t kL1X = 20935;   // Pointer (690,350)
constexpr std::uint16_t kL1Y = 4803;

constexpr std::uint16_t kDpadUpX = 12743;    // Pointer (420,466)
constexpr std::uint16_t kDpadUpY = 6394;
constexpr std::uint16_t kDpadDownX = 6372;   // Pointer (210,466)
constexpr std::uint16_t kDpadDownY = 6394;
constexpr std::uint16_t kDpadLeftX = 10316;  // Pointer (340,380)
constexpr std::uint16_t kDpadLeftY = 5214;
constexpr std::uint16_t kDpadRightX = 10316; // Pointer (340,560)
constexpr std::uint16_t kDpadRightY = 7684;

// Mouse-look is constrained to the user's measured rectangle:
//   Pointer X: 135..610
//   Pointer Y: 1640..2160
//
// Converted HID bounds:
//   X: 4096..18508
//   Y: 22504..29639
//
// The V5 mouse speed was ~3 screen pixels/count. V6 is exactly half that
// step size (~1.5 screen pixels/count) while preserving raw relative motion.
constexpr std::uint8_t kLookContactId = 4;
constexpr std::uint16_t kLookCenterX = 11302; // Pointer center (372.5,1900)
constexpr std::uint16_t kLookCenterY = 26072;
constexpr std::uint16_t kLookMinX = 4096;
constexpr std::uint16_t kLookMaxX = 18508;
constexpr std::uint16_t kLookMinY = 22504;
constexpr std::uint16_t kLookMaxY = 29639;
constexpr std::int32_t kLookMaxHorizontalStep = 984;
constexpr std::int32_t kLookMaxVerticalStep = 2184;

constexpr std::uint8_t kMouseLeftContactId = 5;
constexpr std::uint8_t kTriangleShortContactId = 6;
constexpr std::uint8_t kTriangleHoldContactId = 7;
constexpr std::uint8_t kShareContactId = 8;
constexpr std::uint8_t kR1ContactId = 9;
constexpr std::uint8_t kL1ContactId = 10;
constexpr std::uint8_t kDpadUpContactId = 11;
constexpr std::uint8_t kDpadDownContactId = 12;
constexpr std::uint8_t kDpadLeftContactId = 13;
constexpr std::uint8_t kDpadRightContactId = 14;

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

    // Contact 0 remains the verified left movement / R3 control. D-pad is now
    // intentionally separate because the user supplied four dedicated D-pad
    // touchscreen targets.
    if (state.buttons & ButtonRightStick) {
        append(frame, 0, kR3X, kR3Y);
    } else {
        const bool left = state.lx <= -kStickDeadzone;
        const bool right = state.lx >= kStickDeadzone;

        if (left != right) {
            append(
                frame,
                0,
                left ? kMoveLeftX : kMoveRightX,
                left ? kMoveLeftY : kMoveRightY
            );
        }
    }

    // Cross / jump.
    if (state.buttons & ButtonSouth) {
        append(frame, 1, kCrossX, kCrossY);
    }

    // Square remains on the previously verified target.
    if (state.buttons & ButtonWest) {
        append(frame, 2, kSquareX, kSquareY);
    }

    // Physical/controller R2 remains distinct from mouse-left in V6.
    if (state.rightTrigger > kTriggerThreshold) {
        append(frame, 3, kR2X, kR2Y);
    }

    // Relative mouse-look finger. Motion is constrained to the measured box.
    // On an edge crossing, omit contact 4 for one report so the V4 lifecycle
    // emits a clean UP, then the next motion report re-anchors at box center.
    if (axisActive(state.rx, state.ry)) {
        if (!lookTouchActive_) {
            lookTouchActive_ = true;
            lookTouchX_ = kLookCenterX;
            lookTouchY_ = kLookCenterY;
        }

        const std::int32_t horizontalStep =
            axisToStep(state.rx, kLookMaxHorizontalStep);

        const std::int32_t verticalStep =
            axisToStep(state.ry, kLookMaxVerticalStep);

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
    } else if (
        (state.buttons & kMobileTouchMouseLookHoldButton) &&
        lookTouchActive_
    ) {
        // Keep the current finger down without replaying the last mouse delta.
        append(
            frame,
            kLookContactId,
            lookTouchX_,
            lookTouchY_
        );
    } else {
        resetLookTouch();
    }

    // Mouse-left is now its own touch target, no longer piggybacking R2.
    if (state.buttons & kMobileTouchMouseLeftButton) {
        append(
            frame,
            kMouseLeftContactId,
            kMouseLeftX,
            kMouseLeftY
        );
    }

    if (state.buttons & kMobileTouchTriangleShortButton) {
        append(
            frame,
            kTriangleShortContactId,
            kTriangleShortX,
            kTriangleShortY
        );
    }

    if (state.buttons & kMobileTouchTriangleHoldButton) {
        append(
            frame,
            kTriangleHoldContactId,
            kTriangleHoldX,
            kTriangleHoldY
        );
    }

    // Share supports both the dedicated Share semantic and legacy Back/View.
    if (state.buttons & (ButtonShare | ButtonBack)) {
        append(frame, kShareContactId, kShareX, kShareY);
    }

    if (state.buttons & ButtonRightBumper) {
        append(frame, kR1ContactId, kR1X, kR1Y);
    }

    if (state.buttons & ButtonLeftBumper) {
        append(frame, kL1ContactId, kL1X, kL1Y);
    }

    // Dedicated four-way touchscreen controls. Diagonals intentionally emit
    // two simultaneous fingers because the user supplied four discrete points.
    if (hasDpad(state.dpad, DpadBits::Up)) {
        append(frame, kDpadUpContactId, kDpadUpX, kDpadUpY);
    }
    if (hasDpad(state.dpad, DpadBits::Down)) {
        append(frame, kDpadDownContactId, kDpadDownX, kDpadDownY);
    }
    if (hasDpad(state.dpad, DpadBits::Left)) {
        append(frame, kDpadLeftContactId, kDpadLeftX, kDpadLeftY);
    }
    if (hasDpad(state.dpad, DpadBits::Right)) {
        append(frame, kDpadRightContactId, kDpadRightX, kDpadRightY);
    }

    return frame;
}

} // namespace oag
