#include "addons/universal_defense_combo.h"
#include <cstring>

UniversalDefenseCombo& UniversalDefenseCombo::getInstance() {
    static UniversalDefenseCombo instance;
    return instance;
}

UniversalDefenseCombo::UniversalDefenseCombo() {
    resetAll();
}

void UniversalDefenseCombo::reset(uint8_t slotId) {
    if (slotId < DEFENSE_COMBO_MAX_SLOTS) {
        slots[slotId] = DefenseComboSlotState {};
    }
}

void UniversalDefenseCombo::resetAll() {
    for (uint8_t i = 0; i < DEFENSE_COMBO_MAX_SLOTS; i++) {
        slots[i] = DefenseComboSlotState {};
    }
}

void UniversalDefenseCombo::process(GamepadState& state, uint8_t slotId, uint64_t nowUs) {
#if EFOOTBALL_DEFENSE_COMBO_ENABLED
    if (slotId >= DEFENSE_COMBO_MAX_SLOTS) {
        return;
    }

    DefenseComboSlotState& slot = slots[slotId];
    const bool squareDown = (state.buttons & GAMEPAD_MASK_B3) != 0;

    if (!squareDown) {
        // As soon as Square is released, combo ends immediately.
        if (slot.squareHeld) {
            slot = DefenseComboSlotState {};
        }
        return;
    }

    // Square is currently held
    if (!slot.squareHeld) {
        // First moment Square was pressed
        slot.squareHeld = true;
        slot.squarePressStartUs = nowUs;
        slot.comboActive = false;
        slot.comboCycleStartUs = 0;
    }

    const uint64_t holdDurationUs = nowUs - slot.squarePressStartUs;

    // Check if held for more than/equal to 2 seconds
    if (holdDurationUs < DEFENSE_COMBO_HOLD_THRESHOLD_US) {
        // Less than 2 seconds: normal Square behavior, combo not active yet
        slot.comboActive = false;
        return;
    }

    // >= 2 seconds: Combo is ACTIVE!
    if (!slot.comboActive) {
        slot.comboActive = true;
        slot.comboCycleStartUs = nowUs;
    }

    // 1. Hold L2 continuously
    state.buttons |= GAMEPAD_MASK_L2;
    state.lt = 0xFF;

    // 2. Pulse X every 1 second
    const uint64_t cycleElapsedUs = nowUs - slot.comboCycleStartUs;
    if (cycleElapsedUs >= DEFENSE_COMBO_CYCLE_US) {
        // Advance cycle start by full seconds elapsed
        slot.comboCycleStartUs += (cycleElapsedUs / DEFENSE_COMBO_CYCLE_US) * DEFENSE_COMBO_CYCLE_US;
    }

    const uint64_t phaseUs = nowUs - slot.comboCycleStartUs;
    if (phaseUs < DEFENSE_COMBO_PULSE_DURATION_US) {
        // During the 100ms pulse window: press X
        state.buttons |= GAMEPAD_MASK_B1;
    }
#else
    (void)state;
    (void)slotId;
    (void)nowUs;
#endif
}
