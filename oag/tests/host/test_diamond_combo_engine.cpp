#include <cassert>
#include <cstdint>
#include <iostream>

#include "oag/input/gamepad_state.h"
#include "oag/mapping/diamond_combo_engine.h"

int main() {
    using namespace oag;

    std::array<DiamondComboProgram, kDiamondComboSlots> programs {};
    auto& p = programs[0];
    p.enabled = true;
    p.activation = DiamondComboActivationMode::WhileHeld;
    p.repeat = DiamondComboRepeatMode::Once;
    p.passTriggerThrough = false;
    p.cancelOnTriggerRelease = true;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code = static_cast<std::uint16_t>(DiamondLogicalControl::West);

    // eFootball-style example:
    // hold L2 for the whole combo while X/A is pulsed repeatedly.
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;

    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Pulse;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 30;
    p.steps[1].intervalMs = 70;
    p.steps[1].repeatCount = 0; // until cancelled

    DiamondComboEngine engine;
    LogicalGamepadState input {};
    input.connected = true;
    input.buttons = ButtonWest;

    auto out = engine.apply(programs, nullptr, nullptr, input, 1000);
    assert((out.buttons & ButtonWest) == 0); // trigger consumed
    assert((out.buttons & ButtonSouth) != 0);
    assert(out.leftTrigger == 0xFFFFu);

    out = engine.apply(programs, nullptr, nullptr, input, 41000);
    assert((out.buttons & ButtonSouth) == 0);
    assert(out.leftTrigger == 0xFFFFu);

    out = engine.apply(programs, nullptr, nullptr, input, 111000);
    assert((out.buttons & ButtonSouth) != 0);
    assert(out.leftTrigger == 0xFFFFu);

    // Releasing Square/West cancels immediately and releases generated holds.
    input.buttons = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 121000);
    assert((out.buttons & ButtonSouth) == 0);
    assert(out.leftTrigger == 0);

    // Horizontal editor contract: HOLD + finite PRESS. Once the last action
    // finishes, the HOLD remains latched until the WhileHeld trigger is lifted.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::WhileHeld;
    p.cancelOnTriggerRelease = true;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::East);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Press;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 20;

    input = {};
    input.connected = true;
    input.buttons = ButtonEast;
    out = engine.apply(programs, nullptr, nullptr, input, 150000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) != 0);
    out = engine.apply(programs, nullptr, nullptr, input, 180000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) == 0);
    assert(engine.active());

    input.buttons = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 190000);
    assert(out.leftTrigger == 0);
    assert(!engine.active());

    // Simple V7 editor contract:
    // IF Circle = L2 HOLD (next action after 0.2s) + A PULSE.
    // L2 must remain held through the delay and pulse timeline, and releasing
    // Circle must cancel immediately and release L2.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::WhileHeld;
    p.cancelOnTriggerRelease = true;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::East);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;
    p.steps[0].delayAfterMs = 200;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Pulse;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 50;
    p.steps[1].intervalMs = 50;
    p.steps[1].repeatCount = 3;

    input = {};
    input.connected = true;
    input.buttons = ButtonEast;
    out = engine.apply(programs, nullptr, nullptr, input, 400000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) == 0);

    out = engine.apply(programs, nullptr, nullptr, input, 500000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) == 0);

    out = engine.apply(programs, nullptr, nullptr, input, 600000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) != 0);

    input.buttons = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 610000);
    assert(out.leftTrigger == 0);
    assert((out.buttons & ButtonSouth) == 0);
    assert(!engine.active());

    // Press-once sequence with a wait-until-release condition.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::PressOnce;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code = static_cast<std::uint16_t>(DiamondLogicalControl::East);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::WaitUntilReleased;
    p.steps[0].control = DiamondLogicalControl::East;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Press;
    p.steps[1].control = DiamondLogicalControl::North;
    p.steps[1].durationMs = 25;

    input.buttons = ButtonEast;
    out = engine.apply(programs, nullptr, nullptr, input, 200000);
    assert((out.buttons & ButtonNorth) == 0);

    input.buttons = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 210000);
    assert((out.buttons & ButtonNorth) != 0);

    out = engine.apply(programs, nullptr, nullptr, input, 240000);
    assert((out.buttons & ButtonNorth) == 0);

    // V5: one movement can press multiple controller + keyboard + mouse
    // controls together, including scroll wheel.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::PressOnce;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::South);
    p.stepCount = 1;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::Press;
    p.steps[0].logicalMask =
        (1u << static_cast<std::uint8_t>(DiamondLogicalControl::LeftTrigger)) |
        (1u << static_cast<std::uint8_t>(DiamondLogicalControl::West));
    p.steps[0].keyboardModifiers = 0x02u;
    p.steps[0].keyboardKeys[0] = 0x0Du; // J
    p.steps[0].mouseButtons = MouseButtonMiddle;
    p.steps[0].mouseWheel = 1;
    p.steps[0].durationMs = 40;
    p.steps[0].delayAfterMs = 20;

    input = {};
    input.connected = true;
    input.buttons = ButtonSouth;
    out = engine.apply(programs, nullptr, nullptr, input, 300000);
    assert((out.buttons & ButtonWest) != 0);
    assert(out.leftTrigger == 0xFFFFu);
    assert(engine.nativeOutput().keyboard.pressed(0x0Du));
    assert((engine.nativeOutput().keyboard.modifiers & 0x02u) != 0);
    assert((engine.nativeOutput().mouse.buttons & MouseButtonMiddle) != 0);
    assert(engine.nativeOutput().mouse.wheel == 1);

    engine.consumeNativeWheel();
    assert(engine.nativeOutput().mouse.wheel == 0);

    std::cout << "OAG_DIAMOND_COMBO_ENGINE_TESTS=PASS\n";
    return 0;
}
