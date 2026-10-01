#include <cassert>

#include "oag/config/controller_calibration.h"

int main() {
    using oag::ControllerCalibration;
    using oag::ControllerCalibrationFilter;
    using oag::UniversalGamepadState;

    assert(ControllerCalibrationFilter::applyAxis(0, 0, 1800) == 0);
    assert(ControllerCalibrationFilter::applyAxis(900, 0, 1800) == 0);
    assert(ControllerCalibrationFilter::applyAxis(-900, 0, 1800) == 0);
    assert(ControllerCalibrationFilter::applyAxis(620, 620, 600) == 0);
    assert(ControllerCalibrationFilter::applyAxis(-310, -310, 600) == 0);
    assert(ControllerCalibrationFilter::applyAxis(8000, 620, 600) > 0);
    assert(ControllerCalibrationFilter::applyAxis(-8000, 620, 600) < 0);

    UniversalGamepadState state {};
    state.connected = true;
    state.lx = 620;
    state.ly = -310;
    state.rx = 5000;
    state.ry = -5000;

    ControllerCalibration calibration {};
    calibration.enabled = true; // Calibration is opt-in.
    calibration.left.centerX = 620;
    calibration.left.centerY = -310;
    calibration.left.deadzone = 600;
    calibration.right.deadzone = 600;

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
