#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kLookAxisDeadzone = 0x01000000;

// Android Pointer Location natural space: 1080 x 2388.
// Conversion:
//   HID_X = round(pointer_x * 32768 / 1080)
//   HID_Y = round(pointer_y * 32768 / 2388)

// Camera / mouse-look rectangle supplied by the user:
//   Pointer X = 135..610
//   Pointer Y = 1640..2160
constexpr std::uint8_t kMouseLookContactId = 0;
constexpr std::uint16_t kMouseLookCenterX = 11302;
constexpr std::uint16_t kMouseLookCenterY = 26072;
constexpr std::uint16_t kMouseLookMinX = 4096;
constexpr std::uint16_t kMouseLookMaxX = 18508;
constexpr std::uint16_t kMouseLookMinY = 22504;
constexpr std::uint16_t kMouseLookMaxY = 29639;

// Half V5 camera sensitivity.
constexpr std::int32_t kMouseLookMaxHorizontalStep = 984;
constexpr std::int32_t kMouseLookMaxVerticalStep = 2184;

// PUBG movement joystick.
//
// The four measured direction points share an exact orthogonal crossing:
//   Up    (420,466)
//   Down  (210,466)
//   Left  (340,380)
//   Right (340,560)
//
// Therefore the joystick center is the intersection (340,466). WASD is a
// single persistent finger: touch center first, then drag the SAME contact ID
// to the requested endpoint and hold it there until all movement keys release.
constexpr std::uint8_t kMovementContactId = 1;
constexpr std::uint16_t kMovementCenterX = 10316; // Pointer (340,466)
constexpr std::uint16_t kMovementCenterY = 6394;

constexpr std::uint16_t kMoveUpX = 12743;    // Pointer (420,466)
constexpr std::uint16_t kMoveUpY = 6394;
constexpr std::uint16_t kMoveDownX = 6372;   // Pointer (210,466)
constexpr std::uint16_t kMoveDownY = 6394;
constexpr std::uint16_t kMoveLeftX = 10316;  // Pointer (340,380)
constexpr std::uint16_t kMoveLeftY = 5214;
constexpr std::uint16_t kMoveRightX = 10316; // Pointer (340,560)
constexpr std::uint16_t kMoveRightY = 7684;

// Diagonal targets combine the measured horizontal/vertical endpoints around
// that same center.
constexpr std::uint16_t kMoveUpLeftX = 12743;
constexpr std::uint16_t kMoveUpLeftY = 5214;
constexpr std::uint16_t kMoveUpRightX = 12743;
constexpr std::uint16_t kMoveUpRightY = 7684;
constexpr std::uint16_t kMoveDownLeftX = 6372;
constexpr std::uint16_t kMoveDownLeftY = 5214;
constexpr std::uint16_t kMoveDownRightX = 6372;
constexpr std::uint16_t kMoveDownRightY = 7684;

// Independent actions. Camera and fire never share a contact.
constexpr std::uint8_t kFireContactId = 2;
constexpr std::uint16_t kFireX = 16869; // Pointer (556,2292)
constexpr std::uint16_t kFireY = 31451;

constexpr std::uint8_t kJumpContactId = 3;
constexpr std::uint16_t kJumpX = 10407; // Pointer (343,2230)
constexpr std::uint16_t kJumpY = 30600;

constexpr std::uint8_t kSquareContactId = 4;
constexpr std::uint16_t kSquareX = 6372; // Pointer (210,2080)
constexpr std::uint16_t kSquareY = 28542;

constexpr std::uint8_t kTriangleShortContactId = 5;
constexpr std::uint16_t kTriangleShortX = 2124; // Pointer (70,2016)
constexpr std::uint16_t kTriangleShortY = 27663;

constexpr std::uint8_t kTriangleHoldContactId = 6;
constexpr std::uint16_t kTriangleHoldX = 2427; // Pointer (80,2144)
constexpr std::uint16_t kTriangleHoldY = 29420;

constexpr std::uint8_t kR1ContactId = 7;
constexpr std::uint16_t kR1X = 20935; // Pointer (690,510)
constexpr std::uint16_t kR1Y = 6998;

constexpr std::uint8_t kL1ContactId = 8;
constexpr std::uint16_t kL1X = 20935; // Pointer (690,350)
constexpr std::uint16_t kL1Y = 4803;

constexpr std::uint8_t kShareContactId = 9;
constexpr std::uint16_t kShareX = 3034; // Pointer (100,215)
constexpr std::uint16_t kShareY = 2950;

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

void MobileTouchMapper::resetLookTouch() {
    lookTouchActive_ = false;
    lookTouchX_ = kMouseLookCenterX;
    lookTouchY_ = kMouseLookCenterY;
}

void MobileTouchMapper::resetMovementTouch() {
    movementTouchActive_ = false;
}

MobileTouchFrame MobileTouchMapper::map(
    const LogicalGamepadState& state
) {
    MobileTouchFrame frame {};

    if (!state.connected) {
        resetLookTouch();
        resetMovementTouch();
        return frame;
    }

    // CAMERA: relative mouse drag. A zero-delta report can explicitly request
    // "hold current camera finger" so firing or keyboard events do not release
    // or move the camera contact.
    if (axisActive(state.rx, state.ry)) {
        if (!lookTouchActive_) {
            lookTouchActive_ = true;
            lookTouchX_ = kMouseLookCenterX;
            lookTouchY_ = kMouseLookCenterY;
        }

        const std::int32_t horizontalStep =
            axisToStep(
                state.rx,
                kMouseLookMaxHorizontalStep
            );

        const std::int32_t verticalStep =
            axisToStep(
                state.ry,
                kMouseLookMaxVerticalStep
            );

        const std::int64_t candidateX =
            static_cast<std::int64_t>(lookTouchX_) -
            verticalStep;

        const std::int64_t candidateY =
            static_cast<std::int64_t>(lookTouchY_) +
            horizontalStep;

        if (
            candidateX < kMouseLookMinX ||
            candidateX > kMouseLookMaxX ||
            candidateY < kMouseLookMinY ||
            candidateY > kMouseLookMaxY
        ) {
            resetLookTouch();
        } else {
            lookTouchX_ = clampCoord(candidateX);
            lookTouchY_ = clampCoord(candidateY);

            append(
                frame,
                kMouseLookContactId,
                lookTouchX_,
                lookTouchY_
            );
        }
    } else if (
        (state.buttons & kPubgMouseLookHoldButton) != 0 &&
        lookTouchActive_
    ) {
        append(
            frame,
            kMouseLookContactId,
            lookTouchX_,
            lookTouchY_
        );
    } else {
        resetLookTouch();
    }

    // MOVEMENT: one joystick finger. On the first WASD frame, press the
    // joystick center. The next frame (forced by firmware maintenance if no
    // other input arrives) drags the SAME contact ID to the endpoint.
    const bool up =
        hasDpad(state.dpad, DpadBits::Up) &&
        !hasDpad(state.dpad, DpadBits::Down);

    const bool down =
        hasDpad(state.dpad, DpadBits::Down) &&
        !hasDpad(state.dpad, DpadBits::Up);

    const bool left =
        hasDpad(state.dpad, DpadBits::Left) &&
        !hasDpad(state.dpad, DpadBits::Right);

    const bool right =
        hasDpad(state.dpad, DpadBits::Right) &&
        !hasDpad(state.dpad, DpadBits::Left);

    const bool movementRequested =
        up || down || left || right;

    if (!movementRequested) {
        resetMovementTouch();
    } else if (!movementTouchActive_) {
        movementTouchActive_ = true;
        append(
            frame,
            kMovementContactId,
            kMovementCenterX,
            kMovementCenterY
        );
    } else {
        std::uint16_t x = kMovementCenterX;
        std::uint16_t y = kMovementCenterY;

        if (up) {
            x = kMoveUpX;
        } else if (down) {
            x = kMoveDownX;
        }

        if (left) {
            y = kMoveLeftY;
        } else if (right) {
            y = kMoveRightY;
        }

        append(
            frame,
            kMovementContactId,
            x,
            y
        );
    }

    // FIRE is completely independent from camera movement.
    if (state.buttons & kPubgMouseLeftButton) {
        append(
            frame,
            kFireContactId,
            kFireX,
            kFireY
        );
    }

    if (state.buttons & ButtonSouth) {
        append(
            frame,
            kJumpContactId,
            kJumpX,
            kJumpY
        );
    }

    if (state.buttons & ButtonWest) {
        append(
            frame,
            kSquareContactId,
            kSquareX,
            kSquareY
        );
    }

    if (state.buttons & kPubgTriangleShortButton) {
        append(
            frame,
            kTriangleShortContactId,
            kTriangleShortX,
            kTriangleShortY
        );
    }

    if (state.buttons & kPubgTriangleHoldButton) {
        append(
            frame,
            kTriangleHoldContactId,
            kTriangleHoldX,
            kTriangleHoldY
        );
    }

    if (state.buttons & ButtonRightBumper) {
        append(frame, kR1ContactId, kR1X, kR1Y);
    }

    if (state.buttons & ButtonLeftBumper) {
        append(frame, kL1ContactId, kL1X, kL1Y);
    }

    if (state.buttons & ButtonShare) {
        append(
            frame,
            kShareContactId,
            kShareX,
            kShareY
        );
    }

    return frame;
}

} // namespace oag
