#include "oag/output/universal_output_mode.h"

namespace oag {

UniversalOutputRouting UniversalOutputModeRouter::routing() const {
    switch (mode_) {
        case UniversalOutputMode::Touchscreen:
            return {
                .gamepad = false,
                .touchscreen = true,
                .penTablet = false,
                .keyboard = false,
                .mouse = false,
                .bluetoothPeripheral = true,
            };

        case UniversalOutputMode::DrawingTablet:
            return {
                .gamepad = false,
                .touchscreen = false,
                .penTablet = true,
                .keyboard = false,
                .mouse = false,
                .bluetoothPeripheral = true,
            };

        case UniversalOutputMode::MouseKeyboard:
            return {
                .gamepad = false,
                .touchscreen = false,
                .penTablet = false,
                .keyboard = true,
                .mouse = true,
                .bluetoothPeripheral = true,
            };

        case UniversalOutputMode::Hybrid:
            return {
                .gamepad = true,
                .touchscreen = true,
                .penTablet = true,
                .keyboard = true,
                .mouse = true,
                .bluetoothPeripheral = true,
            };

        case UniversalOutputMode::Gamepad:
        default:
            return {
                .gamepad = true,
                .touchscreen = false,
                .penTablet = false,
                .keyboard = false,
                .mouse = false,
                .bluetoothPeripheral = true,
            };
    }
}

} // namespace oag
