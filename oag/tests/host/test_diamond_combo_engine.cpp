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

    std::cout << "OAG_DIAMOND_COMBO_ENGINE_TESTS=PASS\n";
    return 0;
}
