#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::int32_t kStickDeadzone = 0x18000000;
constexpr std::uint32_t kTriggerThreshold = 0x10000000u;

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
    if (!axisActive(x, y)) {
        return;
    }

    constexpr std::int64_t kAxisMax =
        std::numeric_limits<std::int32_t>::max();

    const std::int64_t dx =
        static_cast<std::int64_t>(x) * radius / kAxisMax;
    const std::int64_t dy =
        static_cast<std::int64_t>(y) * radius / kAxisMax;

    append(
        frame,
        id,
        clampCoord(static_cast<std::int64_t>(centerX) + dx),
        clampCoord(static_cast<std::int64_t>(centerY) + dy)
    );
}

MobileTouchFrame MobileTouchMapper::map(
    const LogicalGamepadState& state
) const {
    MobileTouchFrame frame {};

    if (!state.connected) {
        return frame;
    }

    std::int32_t moveX = state.lx;
    std::int32_t moveY = state.ly;

    if (!axisActive(moveX, moveY)) {
        constexpr std::int32_t kDpadAxis =
            std::numeric_limits<std::int32_t>::max();

        if (hasDpad(state.dpad, DpadBits::Left)) {
            moveX = -kDpadAxis;
        } else if (hasDpad(state.dpad, DpadBits::Right)) {
            moveX = kDpadAxis;
        }

        if (hasDpad(state.dpad, DpadBits::Up)) {
            moveY = -kDpadAxis;
        } else if (hasDpad(state.dpad, DpadBits::Down)) {
            moveY = kDpadAxis;
        }
    }

    // Default normalized mobile-gaming layout. Coordinates intentionally use
    // the HID logical range 0..32767 instead of pixels so the same firmware
    // scales across phone resolutions and orientations.
    appendStick(frame, 0, moveX, moveY, 6500, 24500, 4300);
    appendStick(frame, 1, state.rx, state.ry, 23500, 15500, 5200);

    if (state.buttons & ButtonSouth) append(frame, 2, 27000, 26000);
    if (state.buttons & ButtonEast)  append(frame, 3, 30500, 22500);
    if (state.buttons & ButtonWest)  append(frame, 4, 23500, 22500);
    if (state.buttons & ButtonNorth) append(frame, 5, 27000, 19000);

    if (state.buttons & ButtonLeftBumper)  append(frame, 6, 5500, 3500);
    if (state.buttons & ButtonRightBumper) append(frame, 7, 27200, 3500);

    if (state.leftTrigger > kTriggerThreshold)  append(frame, 8, 3500, 7600);
    if (state.rightTrigger > kTriggerThreshold) append(frame, 9, 29500, 7600);

    if (state.buttons & ButtonStart)      append(frame, 10, 18100, 5200);
    if (state.buttons & ButtonBack)       append(frame, 11, 14600, 5200);
    if (state.buttons & ButtonLeftStick)  append(frame, 12, 9000, 13500);
    if (state.buttons & ButtonRightStick) append(frame, 13, 21800, 13500);
    if (state.buttons & ButtonGuide)      append(frame, 14, 16384, 2600);
    if (state.buttons & ButtonShare)      append(frame, 15, 16384, 8800);

    return frame;
}

} // namespace oag
