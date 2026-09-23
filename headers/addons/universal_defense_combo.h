#pragma once

#include <stdint.h>
#include "gamepad/GamepadState.h"
#include "pico/time.h"

#ifndef EFOOTBALL_DEFENSE_COMBO_ENABLED
#define EFOOTBALL_DEFENSE_COMBO_ENABLED 1
#endif

// ============================================================================
// OAG eFootball Defense Combo
// ============================================================================
// Logic:
// 1. Normal Square presses (< 2.0 seconds) remain 100% normal.
// 2. If Square (GAMEPAD_MASK_B3) is held continuously for >= 2.0 seconds:
//    - L2 (GAMEPAD_MASK_L2 & lt = 0xFF) is held continuously.
//    - X (GAMEPAD_MASK_B1) is pulsed every 1.0 second (100ms ON, 900ms OFF).
// 3. The moment Square is released, the combo ends immediately.
// ============================================================================

static constexpr uint8_t DEFENSE_COMBO_MAX_SLOTS = 5; // Slots 0..3 (USB/BT) + Slot 4 (Local Gamepad)
static constexpr uint64_t DEFENSE_COMBO_HOLD_THRESHOLD_US = 2000000ULL; // 2.0 seconds
static constexpr uint64_t DEFENSE_COMBO_CYCLE_US = 1000000ULL;          // 1.0 second
static constexpr uint64_t DEFENSE_COMBO_PULSE_DURATION_US = 100000ULL;  // 100 ms

struct DefenseComboSlotState {
    bool squareHeld = false;
    uint64_t squarePressStartUs = 0;
    bool comboActive = false;
    uint64_t comboCycleStartUs = 0;
};

class UniversalDefenseCombo {
public:
    UniversalDefenseCombo(UniversalDefenseCombo const&) = delete;
    void operator=(UniversalDefenseCombo const&) = delete;

    static UniversalDefenseCombo& getInstance();

    void reset(uint8_t slotId);
    void resetAll();

    void process(GamepadState& state, uint8_t slotId, uint64_t nowUs);

private:
    UniversalDefenseCombo();
    DefenseComboSlotState slots[DEFENSE_COMBO_MAX_SLOTS] {};
};

#define UDEFENSECOMBO UniversalDefenseCombo::getInstance()
