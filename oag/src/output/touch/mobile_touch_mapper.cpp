#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x28000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

// Game 1 calibration source:
// screenshot 1910x864, target phone landscape 2388x1080.
// HID touchscreen coordinates are normalized to 0..32767.
//
// Screenshot -> physical target -> HID normalized:
// Left arrow  : (183,683) -> (229,854)  -> (3139,25903)
// Center / R3 : (356,681) -> (445,851)  -> (6107,25827)
// Right arrow : (506,684) -> (633,855)  -> (8681,25941)
// Cross attack: (1664,706)-> (2080,882) -> (28547,26775)
constexpr std::uint16_t kMoveLeftX = 3139;
constexpr std::uint16_t kMoveLeftY = 25903;
constexpr std::uint16_t kMoveCenterX = 6107;
constexpr std::uint16_t kMoveCenterY = 25827;
constexpr std::uint16_t kMoveRightX = 8681;
constexpr std::uint16_t kMoveRightY = 25941;
constexpr std::uint16_t kCrossAttackX = 28547;
constexpr std::uint16_t kCrossAttackY = 26775;

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
    // Kept for the generic mapper interface. Game 1 intentionally uses
    // digital left/right touch points because the on-screen control is a
    // side-scroller rocker rather than a free analog joystick.
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

    // One contact only on the left rocker:
    // - R3 click -> center
    // - D-pad/left-stick left -> left arrow
    // - D-pad/left-stick right -> right arrow
    //
    // R3 has priority so the touchscreen never receives two simultaneous
    // fingers on the same virtual rocker.
    if (state.buttons & ButtonRightStick) {
        append(frame, 0, kMoveCenterX, kMoveCenterY);
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

    // PlayStation Cross / logical South hits the large attack button marked
    // by the user. RT shares the same touch target so a physical R2 or the
    // existing mouse-left -> RT binding can attack without a separate profile.
    if (
        (state.buttons & ButtonSouth) ||
        state.rightTrigger > kTriggerThreshold
    ) {
        append(frame, 1, kCrossAttackX, kCrossAttackY);
    }

    return frame;
}

} // namespace oag
