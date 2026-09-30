#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_persistent_config.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct DiamondComboNativeFrame {
    KeyboardState keyboard {};
    MouseState mouse {};
};

class DiamondComboEngine {
public:
    void reset();
    bool active() const;

    LogicalGamepadState apply(
        const std::array<DiamondComboProgram, kDiamondComboSlots>& programs,
        const KeyboardState* keyboard,
        const MouseState* mouse,
        LogicalGamepadState base,
        std::uint64_t nowUs
    );

    const DiamondComboNativeFrame& nativeOutput() const {
        return nativeOutput_;
    }

    void consumeNativeWheel() {
        nativeOutput_.mouse.wheel = 0;
    }

private:
    struct Runtime {
        bool active = false;
        bool previousTrigger = false;
        std::uint8_t stepIndex = 0;
        std::uint64_t phaseStartedUs = 0;
        std::uint64_t nextStepNotBeforeUs = 0;
        bool pulseDown = true;
        bool wheelSent = false;
        std::uint16_t pulseCount = 0;

        std::uint32_t heldControls = 0;
        std::uint8_t heldModifiers = 0;
        std::array<std::uint8_t, 6> heldKeys {};
        std::uint16_t heldMouseButtons = 0;

        // Subset of held outputs whose lifetime is "UNTIL COMBO END".
        // They are released automatically when the visible sequence finishes.
        std::uint32_t endHeldControls = 0;
        std::uint8_t endHeldModifiers = 0;
        std::array<std::uint8_t, 6> endHeldKeys {};
        std::uint16_t endHeldMouseButtons = 0;
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

    static std::uint32_t stepLogicalMask(const DiamondComboStep& step);
    static void applyLogicalMask(
        std::uint32_t mask,
        bool down,
        LogicalGamepadState& state,
        const LogicalGamepadState* releasedBase = nullptr
    );
    static void mergeKey(
        std::array<std::uint8_t, 6>& keys,
        std::uint8_t usage
    );
    static void removeKey(
        std::array<std::uint8_t, 6>& keys,
        std::uint8_t usage
    );

    void applyHeld(
        const Runtime& runtime,
        LogicalGamepadState& output
    );
    void applyStepChord(
        const DiamondComboStep& step,
        Runtime& runtime,
        LogicalGamepadState& output,
        bool includeWheel
    );
    void holdStep(const DiamondComboStep& step, Runtime& runtime);
    void releaseStep(const DiamondComboStep& step, Runtime& runtime);
    void advance(
        const DiamondComboStep& step,
        Runtime& runtime,
        std::uint64_t nowUs
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
    DiamondComboNativeFrame nativeOutput_ {};
};

} // namespace oag
