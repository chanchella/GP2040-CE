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

// Mouse look rectangle requested by the user:
//   X = 135..610
//   Y = 1640..2160
constexpr std::uint8_t kMouseLookContactId = 0;
constexpr std::uint16_t kMouseLookCenterX = 11302;
constexpr std::uint16_t kMouseLookCenterY = 26072;
constexpr std::uint16_t kMouseLookMinX = 4096;
constexpr std::uint16_t kMouseLookMaxX = 18508;
constexpr std::uint16_t kMouseLookMinY = 22504;
constexpr std::uint16_t kMouseLookMaxY = 29639;

// Exactly half the V5 movement step.
constexpr std::int32_t kMouseLookMaxHorizontalStep = 984;
constexpr std::int32_t kMouseLookMaxVerticalStep = 2184;

// PUBG measured targets.
constexpr std::uint8_t kMouseLeftContactId = 1;
constexpr std::uint16_t kMouseLeftX = 16869;
constexpr std::uint16_t kMouseLeftY = 31451;

constexpr std::uint8_t kJumpContactId = 2;
constexpr std::uint16_t kJumpX = 10407;
constexpr std::uint16_t kJumpY = 30600;

constexpr std::uint8_t kSquareContactId = 3;
constexpr std::uint16_t kSquareX = 6372;
constexpr std::uint16_t kSquareY = 28542;

constexpr std::uint8_t kTriangleShortContactId = 4;
constexpr std::uint16_t kTriangleShortX = 2124;
constexpr std::uint16_t kTriangleShortY = 27663;

constexpr std::uint8_t kTriangleHoldContactId = 5;
constexpr std::uint16_t kTriangleHoldX = 2427;
constexpr std::uint16_t kTriangleHoldY = 29420;

constexpr std::uint8_t kR1ContactId = 6;
constexpr std::uint16_t kR1X = 20935;
constexpr std::uint16_t kR1Y = 6998;

constexpr std::uint8_t kL1ContactId = 7;
constexpr std::uint16_t kL1X = 20935;
constexpr std::uint16_t kL1Y = 4803;

constexpr std::uint8_t kShareContactId = 8;
constexpr std::uint16_t kShareX = 3034;
constexpr std::uint16_t kShareY = 2950;

constexpr std::uint8_t kUpContactId = 9;
constexpr std::uint16_t kUpX = 12743;
constexpr std::uint16_t kUpY = 6394;

constexpr std::uint8_t kDownContactId = 10;
constexpr std::uint16_t kDownX = 6372;
constexpr std::uint16_t kDownY = 6394;

constexpr std::uint8_t kLeftContactId = 11;
constexpr std::uint16_t kLeftX = 10316;
constexpr std::uint16_t kLeftY = 5214;

constexpr std::uint8_t kRightContactId = 12;
constexpr std::uint16_t kRightX = 10316;
constexpr std::uint16_t kRightY = 7684;

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

MobileTouchFrame MobileTouchMapper::map(
    const LogicalGamepadState& state
) {
    MobileTouchFrame frame {};

    if (!state.connected) {
        resetLookTouch();
        return frame;
    }

    // Mouse look is a real relative touchscreen drag constrained to the PUBG
    // camera rectangle. When the next movement would cross a boundary, the
    // contact is omitted for one frame so the V4 explicit-UP lifecycle
    // releases it cleanly; the next movement starts again from box center.
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
    } else {
        resetLookTouch();
    }

    if (state.buttons & kPubgMouseLeftButton) {
        append(
            frame,
            kMouseLeftContactId,
            kMouseLeftX,
            kMouseLeftY
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

    if (hasDpad(state.dpad, DpadBits::Up)) {
        append(frame, kUpContactId, kUpX, kUpY);
    }

    if (hasDpad(state.dpad, DpadBits::Down)) {
        append(frame, kDownContactId, kDownX, kDownY);
    }

    if (hasDpad(state.dpad, DpadBits::Left)) {
        append(frame, kLeftContactId, kLeftX, kLeftY);
    }

    if (hasDpad(state.dpad, DpadBits::Right)) {
        append(frame, kRightContactId, kRightX, kRightY);
    }

    return frame;
}

} // namespace oag
