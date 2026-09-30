#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/config/controller_calibration.h"

int main() {
    using oag::ControllerCalibration;
    using oag::ControllerCalibrationFilter;
    using oag::UniversalGamepadState;

    constexpr std::int32_t kFull = std::numeric_limits<std::int32_t>::max();
    constexpr std::uint32_t kTenPercent =
        static_cast<std::uint32_t>(kFull / 10);

    // Full-scale 32-bit canonical axis tests.
    assert(ControllerCalibrationFilter::applyAxis(0, 0, kTenPercent) == 0);
    assert(ControllerCalibrationFilter::applyAxis(kFull / 20, 0, kTenPercent) == 0);
    assert(ControllerCalibrationFilter::applyAxis(-(kFull / 20), 0, kTenPercent) == 0);
    assert(ControllerCalibrationFilter::applyAxis(kFull / 2, 0, kTenPercent) > 0);
    assert(ControllerCalibrationFilter::applyAxis(-(kFull / 2), 0, kTenPercent) < 0);

    // A real center offset around 10% must be removable, matching the scale
    // emitted by XUSB/XGIP/HID input drivers.
    constexpr std::int32_t centerX = 208148688;
    constexpr std::int32_t centerY = -199098368;
    assert(ControllerCalibrationFilter::applyAxis(centerX, centerX, kTenPercent) == 0);
    assert(ControllerCalibrationFilter::applyAxis(centerY, centerY, kTenPercent) == 0);

    UniversalGamepadState state {};
    state.connected = true;
    state.lx = centerX;
    state.ly = centerY;
    state.rx = kFull / 2;
    state.ry = -(kFull / 2);

    ControllerCalibration calibration {};
    calibration.enabled = true;
    calibration.left.centerX = centerX;
    calibration.left.centerY = centerY;
    calibration.left.deadzone = kTenPercent;
    calibration.right.deadzone = kTenPercent;

    const auto filtered = ControllerCalibrationFilter::apply(state, calibration);
    assert(filtered.lx == 0);
    assert(filtered.ly == 0);
    assert(filtered.rx > 0);
    assert(filtered.ry < 0);

    calibration.enabled = false;
    const auto bypassed = ControllerCalibrationFilter::apply(state, calibration);
    assert(bypassed.lx == state.lx);
    assert(bypassed.ly == state.ly);
    return 0;
}
