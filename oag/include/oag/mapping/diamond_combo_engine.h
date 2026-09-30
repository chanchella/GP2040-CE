#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_persistent_config.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

class DiamondComboEngine {
public:
    void reset();

    LogicalGamepadState apply(
        const std::array<DiamondComboProgram, kDiamondComboSlots>& programs,
        const KeyboardState* keyboard,
        const MouseState* mouse,
        LogicalGamepadState base,
        std::uint64_t nowUs
    );

private:
    struct Runtime {
        bool active = false;
        bool previousTrigger = false;
        std::uint8_t stepIndex = 0;
        std::uint64_t phaseStartedUs = 0;
        bool pulseDown = true;
        std::uint16_t pulseCount = 0;
        std::uint32_t heldControls = 0;
    };

    static bool controlActive(
        DiamondLogicalControl control,
        const LogicalGamepadState& state
    );
    static void setControl(
        DiamondLogicalControl control,
        bool down,
        LogicalGamepadState& state
    );
    static bool triggerActive(
        const DiamondComboProgram& program,
        const KeyboardState* keyboard,
        const MouseState* mouse,
        const LogicalGamepadState& state
    );
    static void clearLogicalTriggers(
        const DiamondComboProgram& program,
        LogicalGamepadState& state
    );

    void stop(Runtime& runtime);
    bool execute(
        const DiamondComboProgram& program,
        Runtime& runtime,
        const LogicalGamepadState& input,
        LogicalGamepadState& output,
        std::uint64_t nowUs
    );

    std::array<Runtime, kDiamondComboSlots> runtime_ {};
};

} // namespace oag
