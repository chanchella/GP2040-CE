#include "oag/config/controller_calibration.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace oag {
namespace {

constexpr std::int64_t kAxisMagnitude =
    static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());

std::int32_t clampAxis(std::int64_t value) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()
    ));
}

} // namespace

std::int32_t ControllerCalibrationFilter::applyAxis(
    std::int32_t value,
    std::int32_t center,
    std::uint32_t deadzone) {
    // UniversalGamepadState is normalized to the FULL signed 32-bit axis
    // range by XUSB/XGIP/Generic HID. Calibration must use that same scale.
    const std::int64_t shifted =
        static_cast<std::int64_t>(value) - static_cast<std::int64_t>(center);
    const std::int64_t magnitude = shifted < 0 ? -shifted : shifted;
    const std::int64_t dz = std::min<std::int64_t>(deadzone, kAxisMagnitude);

    if (magnitude <= dz) {
        return 0;
    }

    const std::int64_t available = kAxisMagnitude - dz;
    if (available <= 0) {
        return 0;
    }

    const std::int64_t remaining = magnitude - dz;
    const std::int64_t scaled =
        std::min<std::int64_t>(
            kAxisMagnitude,
            (remaining * kAxisMagnitude) / available
        );

    return clampAxis(shifted < 0 ? -scaled : scaled);
}

UniversalGamepadState ControllerCalibrationFilter::apply(
    const UniversalGamepadState& input,
    const ControllerCalibration& calibration) {
    if (!calibration.enabled || !input.connected) {
        return input;
    }

    UniversalGamepadState output = input;
    output.lx = applyAxis(input.lx, calibration.left.centerX, calibration.left.deadzone);
    output.ly = applyAxis(input.ly, calibration.left.centerY, calibration.left.deadzone);
    output.rx = applyAxis(input.rx, calibration.right.centerX, calibration.right.deadzone);
    output.ry = applyAxis(input.ry, calibration.right.centerY, calibration.right.deadzone);
    return output;
}

} // namespace oag
