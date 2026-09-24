#include "oag/mapping/touchscreen_mapper.h"

#include <algorithm>
#include <cstdint>

namespace oag {
namespace {

constexpr std::int32_t kMouseToAbsoluteScale = 48;

} // namespace

TouchDigitizerState TouchscreenMapper::apply(
    const MouseState& mouse,
    MouseMotion motion
) {
    state_.connected = mouse.connected;
    state_.inRange = mouse.connected;

    state_.x = advance(state_.x, motion.dx);
    state_.y = advance(state_.y, motion.dy);

    state_.contact =
        mouse.connected &&
        (mouse.buttons & MouseButtonLeft) != 0;

    ++state_.generation;
    state_.timestampUs = mouse.timestampUs;
    return state_;
}

void TouchscreenMapper::reset() {
    state_ = {};
    state_.x = kCoordinateMax / 2u;
    state_.y = kCoordinateMax / 2u;
}

std::uint16_t TouchscreenMapper::advance(
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
