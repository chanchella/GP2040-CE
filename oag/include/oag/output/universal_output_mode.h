#pragma once

#include <cstdint>

namespace oag {

enum class UniversalOutputMode : std::uint8_t {
    Gamepad = 0,
    Touchscreen,
    DrawingTablet,
    MouseKeyboard,
    Hybrid,
};

struct UniversalOutputRouting {
    bool gamepad = false;
    bool touchscreen = false;
    bool penTablet = false;
    bool keyboard = false;
    bool mouse = false;
    bool bluetoothPeripheral = false;
};

class UniversalOutputModeRouter {
public:
    UniversalOutputMode mode() const {
        return mode_;
    }

    void setMode(UniversalOutputMode mode) {
        mode_ = mode;
    }

    UniversalOutputRouting routing() const;

private:
    UniversalOutputMode mode_ = UniversalOutputMode::Gamepad;
};

} // namespace oag
