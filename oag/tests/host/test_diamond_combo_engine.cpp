#ifdef NDEBUG
#undef NDEBUG // Keep combo timing assertions active in Release CI builds.
#endif
#include <cassert>
#include <cstdint>
#include <iostream>

#include "oag/input/gamepad_state.h"
#include "oag/mapping/diamond_combo_engine.h"

// Exercise persisted masks and legacy single-control fields for every direction.
static void testRightStickActions() {
    using namespace oag;
    const std::int32_t expected[][2] = {
        {2147483647, 0}, {-2147483647 - 1, 0},
        {0, -2147483647 - 1}, {0, 2147483647},
        {1518500249, -1518500249}, {-1518500249, -1518500249},
        {1518500249, 1518500249}, {-1518500249, 1518500249}
    };
    static_assert(static_cast<unsigned>(DiamondLogicalControl::DpadRight) == 17);
    static_assert(static_cast<unsigned>(DiamondLogicalControl::RightStickDownLeft) == 25);
    for (unsigned direction = 0; direction < 8; ++direction) {
        {
            std::array<DiamondComboProgram, kDiamondComboSlots> programs {};
            auto& p = programs[0];
            p.enabled = true;
            p.activation = DiamondComboActivationMode::WhileHeld;
            p.cancelOnTriggerRelease = true;
            p.passTriggerThrough = false;
            p.triggers[0] = {true, DiamondComboTriggerKind::LogicalControl,
                static_cast<std::uint16_t>(18 + direction), 0};
            p.stepCount = 1;
            p.steps[0].enabled = true;
            p.steps[0].kind = DiamondComboStepKind::HoldStart;
            p.steps[0].control = DiamondLogicalControl::South;
            p.steps[0].durationMs = 0;
            DiamondComboEngine engine;
            LogicalGamepadState input {};
            input.connected = true;
            // Small center drift must not trigger a combo.
            input.rx = 1000000; input.ry = -1000000;
            auto out = engine.apply(programs, nullptr, nullptr, input, 1000);
            assert(!engine.active());
            input.rx = expected[direction][0]; input.ry = expected[direction][1];
            out = engine.apply(programs, nullptr, nullptr, input, 2000);
            assert(engine.active());
            assert((out.buttons & ButtonSouth) != 0);
            assert(out.rx == 0 && out.ry == 0); // trigger consumed
            input.rx = 0; input.ry = 0;
            out = engine.apply(programs, nullptr, nullptr, input, 3000);
            assert(!engine.active());
            assert((out.buttons & ButtonSouth) == 0);
        }
        for (const bool useMask : {false, true}) {
            for (const auto kind : {DiamondComboStepKind::Press,
                    DiamondComboStepKind::HoldStart, DiamondComboStepKind::Pulse}) {
                std::array<DiamondComboProgram, kDiamondComboSlots> programs {};
                auto& p = programs[0];
                p.enabled = true;
                p.activation = DiamondComboActivationMode::PressOnce;
                p.cancelOnTriggerRelease = false;
                p.triggers[0] = {true, DiamondComboTriggerKind::LogicalControl,
                    static_cast<std::uint16_t>(DiamondLogicalControl::South), 0};
                p.stepCount = 1;
                auto& step = p.steps[0];
                step.enabled = true;
                step.kind = kind;
                step.control = useMask ? DiamondLogicalControl::None :
                    static_cast<DiamondLogicalControl>(18 + direction);
                step.logicalMask = useMask ? (1u << (18 + direction)) : 0;
                step.durationMs = 200;
                step.intervalMs = kind == DiamondComboStepKind::Pulse ? 750 : 0;
                step.delayAfterMs = kind == DiamondComboStepKind::Pulse ? 0 : 750;
                step.repeatCount = 0;
                DiamondComboEngine engine;
                LogicalGamepadState input {};
                input.connected = true;
                input.buttons = ButtonSouth | ButtonRightStick;
                input.lx = 123456;
                input.ly = -987654;
                input.rx = 123456789;
                input.ry = -987654321;
                auto out = engine.apply(programs, nullptr, nullptr, input, 1000);
                assert(out.rx == expected[direction][0]);
                assert(out.ry == expected[direction][1]);
                assert(out.lx == input.lx && out.ly == input.ly);
                assert(out.buttons == input.buttons); // R3 remains independent.
                out = engine.apply(programs, nullptr, nullptr, input, 201000);
                assert(out.rx == input.rx && out.ry == input.ry);
                input.rx = -7654321; // release follows live input, not stale input.
                out = engine.apply(programs, nullptr, nullptr, input, 950000);
                assert(out.rx == input.rx && out.ry == input.ry);
                out = engine.apply(programs, nullptr, nullptr, input, 951000);
                if (kind == DiamondComboStepKind::Pulse) {
                    assert(out.rx == expected[direction][0]);
                    assert(out.ry == expected[direction][1]);
                } else {
                    assert(out.rx == input.rx && out.ry == input.ry);
                    assert(!engine.active());
                }
                engine.reset(); // same cancellation used on game changes.
                input.buttons = 0;
                out = engine.apply(programs, nullptr, nullptr, input, 952000);
                assert(out.rx == input.rx && out.ry == input.ry);

                // Hold until combo end restores both axes on the completion frame.
                engine.reset();
                step.kind = DiamondComboStepKind::HoldStart;
                step.durationMs = 0;
                step.delayAfterMs = 65535;
                p.stepCount = 2;
                p.steps[1].enabled = true;
                p.steps[1].kind = DiamondComboStepKind::Wait;
                p.steps[1].durationMs = 20;
                input.buttons = ButtonSouth;
                out = engine.apply(programs, nullptr, nullptr, input, 1000000);
                assert(out.rx == expected[direction][0]);
                out = engine.apply(programs, nullptr, nullptr, input, 1020000);
                assert(out.rx == input.rx && out.ry == input.ry);
                assert(!engine.active());

                // Toggle stop and trigger release must release latched axes.
                for (auto activation : {DiamondComboActivationMode::Toggle,
                        DiamondComboActivationMode::WhileHeld}) {
                    engine.reset();
                    p.activation = activation;
                    p.cancelOnTriggerRelease = activation == DiamondComboActivationMode::WhileHeld;
                    p.stepCount = 1;
                    step.delayAfterMs = 0;
                    input.buttons = ButtonSouth;
                    out = engine.apply(programs, nullptr, nullptr, input, 2000000);
                    assert(out.rx == expected[direction][0]);
                    input.buttons = 0;
                    out = engine.apply(programs, nullptr, nullptr, input, 2001000);
                    if (activation == DiamondComboActivationMode::Toggle) {
                        assert(out.rx == expected[direction][0]);
                        input.buttons = ButtonSouth;
                        out = engine.apply(programs, nullptr, nullptr, input, 2002000);
                    }
                    assert(out.rx == input.rx && out.ry == input.ry);
                    assert(!engine.active());
                }
            }
        }
    }
}

int main() {
    testRightStickActions();
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

    // V9 editor: HOLD for a finite millisecond duration must release
    // before the next action, without changing legacy latched HOLD records.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::PressOnce;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::East);
    p.stepCount = 1;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::South;
    p.steps[0].durationMs = 100;
    p.steps[0].delayAfterMs = 450; // RELEASE / WAIT before next action.
    p.steps[0].intervalMs = 0; // V9 finite-HOLD marker.

    input = {};
    input.connected = true;
    input.buttons = ButtonEast;
    out = engine.apply(programs, nullptr, nullptr, input, 700000);
    assert((out.buttons & ButtonSouth) != 0);
    out = engine.apply(programs, nullptr, nullptr, input, 750000);
    assert((out.buttons & ButtonSouth) != 0);
    out = engine.apply(programs, nullptr, nullptr, input, 810000);
    assert((out.buttons & ButtonSouth) == 0);
    assert(engine.active()); // still inside the 450 ms released interval.
    out = engine.apply(programs, nullptr, nullptr, input, 1200000);
    assert((out.buttons & ButtonSouth) == 0);
    assert(engine.active());
    out = engine.apply(programs, nullptr, nullptr, input, 1270000);
    assert((out.buttons & ButtonSouth) == 0);
    assert(!engine.active());

    // HOLD UNTIL COMBO END is persisted with delayAfterMs=0xFFFF. It must
    // stay down while later actions execute and release on normal completion.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::PressOnce;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::East);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;
    p.steps[0].durationMs = 0;
    p.steps[0].delayAfterMs = 0xFFFFu;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Press;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 20;

    input.buttons = ButtonEast;
    out = engine.apply(programs, nullptr, nullptr, input, 900000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) != 0);
    out = engine.apply(programs, nullptr, nullptr, input, 930000);
    assert(out.leftTrigger == 0);
    assert((out.buttons & ButtonSouth) == 0);
    assert(!engine.active());

    // Ctrl/Shift/Alt can be standalone combo triggers using HID modifier bits.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::PressOnce;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::KeyboardUsage;
    p.triggers[0].code = 0;
    p.triggers[0].modifiers = 0x01u; // Left Ctrl
    p.stepCount = 1;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::Press;
    p.steps[0].control = DiamondLogicalControl::North;
    p.steps[0].durationMs = 20;

    KeyboardState modifierKeyboard {};
    modifierKeyboard.connected = true;
    modifierKeyboard.modifiers = 0x01u;
    input = {};
    input.connected = true;
    out = engine.apply(
        programs,
        &modifierKeyboard,
        nullptr,
        input,
        1000000
    );
    assert((out.buttons & ButtonNorth) != 0);

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

    // eFootball Game 1 / Combo 1 contract:
    // one L2 tap toggles a generated L2 hold plus Cross/South pulse
    // (200 ms down / 750 ms up); the next L2 tap cancels everything.
    engine.reset();
    p = {};
    p.enabled = true;
    p.activation = DiamondComboActivationMode::Toggle;
    p.repeat = DiamondComboRepeatMode::Once;
    p.passTriggerThrough = false;
    p.cancelOnTriggerRelease = false;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::LeftTrigger);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Pulse;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 200;
    p.steps[1].intervalMs = 750;
    p.steps[1].repeatCount = 0;

    input = {};
    input.connected = true;
    input.leftTrigger = 0xFFFFu;
    out = engine.apply(programs, nullptr, nullptr, input, 2000000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) != 0);
    assert(engine.active());

    // Physical L2 may be released; generated L2 remains held.
    input.leftTrigger = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 2210000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) == 0);
    assert(engine.active());

    // 750 ms released interval expires, then Cross/South pulses again.
    out = engine.apply(programs, nullptr, nullptr, input, 2961000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) != 0);
    assert(engine.active());

    // Second physical L2 tap is the Toggle-off edge. The physical L2 report
    // itself is still naturally down on this frame, but the generated hold and
    // Cross pulse is already gone. Releasing the tap leaves L2 fully released.
    input.leftTrigger = 0xFFFFu;
    out = engine.apply(programs, nullptr, nullptr, input, 2970000);
    assert(out.leftTrigger == 0xFFFFu);
    assert((out.buttons & ButtonSouth) == 0);
    assert(!engine.active());

    input.leftTrigger = 0;
    out = engine.apply(programs, nullptr, nullptr, input, 2980000);
    assert(out.leftTrigger == 0);
    assert((out.buttons & ButtonSouth) == 0);
    assert(!engine.active());

    std::cout << "OAG_DIAMOND_COMBO_ENGINE_TESTS=PASS\n";
    return 0;
}
