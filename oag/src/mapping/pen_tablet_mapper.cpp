#include "oag/mapping/pen_tablet_mapper.h"

#include <algorithm>
#include <cstdint>

namespace oag {
namespace {

constexpr std::int32_t kMouseToAbsoluteScale = 48;

} // namespace

PenDigitizerState PenTabletMapper::apply(
    const MouseState& mouse,
    MouseMotion motion
) {
    state_.connected = mouse.connected;
    state_.inRange = mouse.connected;

    state_.x = advance(state_.x, motion.dx);
    state_.y = advance(state_.y, motion.dy);

    state_.tip =
        mouse.connected &&
        (mouse.buttons & MouseButtonLeft) != 0;

    state_.barrel =
        mouse.connected &&
        (mouse.buttons & MouseButtonRight) != 0;

    state_.eraser =
        mouse.connected &&
        (mouse.buttons & MouseButtonMiddle) != 0;

    // Mouse input has no physical pressure sensor. Preserve a real pressure
    // channel in the canonical state so mobile stylus input can populate it
    // later; mouse-as-pen uses binary pressure only.
    state_.pressure = state_.tip ? kPressureMax : 0;

    ++state_.generation;
    state_.timestampUs = mouse.timestampUs;
    return state_;
}

void PenTabletMapper::reset() {
    state_ = {};
    state_.x = kCoordinateMax / 2u;
    state_.y = kCoordinateMax / 2u;
}

std::uint16_t PenTabletMapper::advance(
    std::uint16_t current,
    std::int32_t delta
) {
    const std::int64_t next =
        static_cast<std::int64_t>(current) +
        static_cast<std::int64_t>(delta) *
            kMouseToAbsoluteScale;

    return static_cast<std::uint16_t>(
        std::clamp<std::int64_t>(
            next,
            0,
            kCoordinateMax
        )
    );
}

} // namespace oag
