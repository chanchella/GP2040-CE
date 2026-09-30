#pragma once

#include <cstdint>

#include "oag/config/diamond_config.h"
#include "oag/input/gamepad_state.h"

namespace oag {

class ControllerCalibrationFilter {
public:
    static std::int32_t applyAxis(
        std::int32_t value,
        std::int32_t center,
        std::uint32_t deadzone);

    static UniversalGamepadState apply(
        const UniversalGamepadState& input,
        const ControllerCalibration& calibration);
};

} // namespace oag
