#pragma once

#include <array>
#include <cstdint>

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"

namespace oag::firmware {

class PcNativeKmOutput {
public:
    void setEnabled(bool enabled);

    void updateState(
        const KeyboardState& keyboard,
        const MouseState& mouse
    );

    void addMouseMotion(
        std::int32_t dx,
        std::int32_t dy,
        std::int16_t wheel,
        std::int16_t pan
    );

    void task(std::uint64_t nowUs);
    void releaseAll();

private:
    static constexpr std::uint8_t kKeyboardInstance = 0;
    static constexpr std::uint8_t kMouseInstance = 1;
    static constexpr std::uint64_t kModeChordGraceUs = 120000;

    std::array<std::uint8_t, 8> buildKeyboardReport(
        std::uint64_t nowUs
    );

    bool enabled_ = false;

    KeyboardState keyboard_ {};
    MouseState mouse_ {};

    std::array<std::uint8_t, 8> lastKeyboardReport_ {};
    std::uint8_t lastMouseButtons_ = 0;

    std::int32_t pendingDx_ = 0;
    std::int32_t pendingDy_ = 0;
    std::int32_t pendingWheel_ = 0;
    std::int32_t pendingPan_ = 0;

    std::uint64_t f4PressedSinceUs_ = 0;
    std::uint64_t f5PressedSinceUs_ = 0;

    bool keyboardReleasePending_ = false;
    bool mouseReleasePending_ = false;
};

} // namespace oag::firmware
