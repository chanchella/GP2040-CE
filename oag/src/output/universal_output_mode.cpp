#include "oag/output/universal_output_mode.h"

namespace oag {

UniversalOutputRouting UniversalOutputModeRouter::routing() const {
    switch (mode_) {
        case UniversalOutputMode::Touchscreen:
            return {false, true, false, false, false, true};

        case UniversalOutputMode::DrawingTablet:
            return {false, false, true, false, false, true};

        case UniversalOutputMode::MouseKeyboard:
            return {false, false, false, true, true, true};

        case UniversalOutputMode::Hybrid:
            return {true, true, true, true, true, true};

        case UniversalOutputMode::Gamepad:
        default:
            return {true, false, false, false, false, true};
    }
}

} // namespace oag
