#include "oag/output/touch/mobile_touch_mapper.h"

#include <algorithm>
#include <cstdint>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::uint16_t kDiagnosticCenterX = 16384;
constexpr std::uint16_t kDiagnosticCenterY = 16384;

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
    (void)x;
    (void)y;
    return false;
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
    // Intentionally disabled in the transport diagnostic.
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

    // TOUCH TRANSPORT DIAGNOSTIC ONLY.
    //
    // Hold physical/logical South:
    //   PlayStation Cross / Xbox A / keyboard Space
    //
    // While held, emit exactly one contact at the center of the normalized
    // touchscreen. Releasing the control produces an empty frame, which the
    // existing MobileTouchOutput serializes as contactCount=0.
    //
    // Target:
    //   HID       = (16384, 16384)
    //   2388x1080 = approximately (1194, 540)
    if (state.buttons & ButtonSouth) {
        append(
            frame,
            0,
            kDiagnosticCenterX,
            kDiagnosticCenterY
        );
    }

    return frame;
}

} // namespace oag
