#pragma once

#include <cstdint>

#include "oag/input/mouse_state.h"
#include "oag/output/digitizer/digitizer_state.h"

namespace oag {

class PenTabletMapper {
public:
    static constexpr std::uint16_t kCoordinateMax = 32767;
    static constexpr std::uint16_t kPressureMax = 65535;

    PenDigitizerState apply(
        const MouseState& mouse,
        MouseMotion motion
    );

    void reset();

private:
    static std::uint16_t advance(
        std::uint16_t current,
        std::int32_t delta
    );

    PenDigitizerState state_ {};
};

} // namespace oag
