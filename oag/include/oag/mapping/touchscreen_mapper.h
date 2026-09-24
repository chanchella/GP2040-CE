#pragma once

#include <cstdint>

#include "oag/input/mouse_state.h"
#include "oag/output/digitizer/digitizer_state.h"

namespace oag {

class TouchscreenMapper {
public:
    static constexpr std::uint16_t kCoordinateMax = 32767;

    TouchDigitizerState apply(
        const MouseState& mouse,
        MouseMotion motion
    );

    void reset();

private:
    static std::uint16_t advance(
        std::uint16_t current,
        std::int32_t delta
    );

    TouchDigitizerState state_ {};
};

} // namespace oag
