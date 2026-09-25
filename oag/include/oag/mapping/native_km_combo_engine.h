#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"

namespace oag {

struct NativeKmComboRule {
    bool enabled = false;
    std::uint8_t requiredModifiers = 0;
    std::array<std::uint8_t, 4> requiredKeys {};
    std::uint16_t requiredMouseButtons = 0;

    std::uint8_t outputModifiers = 0;
    std::array<std::uint8_t, 6> outputKeys {};
    std::uint16_t outputMouseButtons = 0;

    bool suppressTriggerInputs = false;
};

struct NativeKmComboFrame {
    KeyboardState keyboard {};
    MouseState mouse {};
};

class NativeKmComboEngine {
public:
    static constexpr std::size_t kComboSlots = 8;

    bool configure(std::size_t slot, const NativeKmComboRule& rule);
    bool disable(std::size_t slot);
    const NativeKmComboRule* rule(std::size_t slot) const;

    NativeKmComboFrame apply(
        const KeyboardState& keyboard,
        const MouseState& mouse
    ) const;

private:
    static bool matches(
        const NativeKmComboRule& rule,
        const KeyboardState& keyboard,
        const MouseState& mouse
    );

    std::array<NativeKmComboRule, kComboSlots> rules_ {};
};

} // namespace oag
