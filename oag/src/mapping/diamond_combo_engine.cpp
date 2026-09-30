#include "oag/mapping/diamond_combo_engine.h"

#include "oag/input/gamepad_state.h"
#include <limits>

namespace {

constexpr std::uint32_t kFullTrigger = 0xFFFFu;
constexpr std::uint32_t kRightStickMask = 0x03FC0000u;
constexpr std::int32_t kAxisMax = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t kAxisMin = std::numeric_limits<std::int32_t>::min();
// Full radial travel at 45 degrees: max / sqrt(2).
constexpr std::int32_t kDiagonal = 1518500249;
constexpr std::uint16_t kHoldUntilComboEndSentinel = 0xFFFFu;

constexpr std::uint32_t controlBit(oag::DiamondLogicalControl control) {
    const auto raw = static_cast<std::uint8_t>(control);
    return raw == 0 || raw >= 32 ? 0u : (1u << raw);
}

} // namespace

namespace oag {

void DiamondComboEngine::reset() {
    runtime_ = {};
    nativeOutput_ = {};
}

bool DiamondComboEngine::active() const {
    for (const auto& runtime : runtime_) {
        if (runtime.active) return true;
    }
    return false;
}

bool DiamondComboEngine::controlActive(
    DiamondLogicalControl control,
    const LogicalGamepadState& state
) {
    // Input-only threshold for newly added combo triggers; no calibration change.
    constexpr std::int32_t threshold = 1073741823;
    const int x = state.rx > threshold ? 1 : (state.rx < -threshold ? -1 : 0);
    const int y = state.ry > threshold ? 1 : (state.ry < -threshold ? -1 : 0);
    switch (control) {
    case DiamondLogicalControl::South: return (state.buttons & ButtonSouth) != 0;
    case DiamondLogicalControl::East: return (state.buttons & ButtonEast) != 0;
    case DiamondLogicalControl::West: return (state.buttons & ButtonWest) != 0;
    case DiamondLogicalControl::North: return (state.buttons & ButtonNorth) != 0;
    case DiamondLogicalControl::LeftBumper: return (state.buttons & ButtonLeftBumper) != 0;
    case DiamondLogicalControl::RightBumper: return (state.buttons & ButtonRightBumper) != 0;
    case DiamondLogicalControl::LeftTrigger: return state.leftTrigger != 0;
    case DiamondLogicalControl::RightTrigger: return state.rightTrigger != 0;
    case DiamondLogicalControl::LeftStickClick: return (state.buttons & ButtonLeftStick) != 0;
    case DiamondLogicalControl::RightStickClick: return (state.buttons & ButtonRightStick) != 0;
    case DiamondLogicalControl::Back: return (state.buttons & ButtonBack) != 0;
    case DiamondLogicalControl::Start: return (state.buttons & ButtonStart) != 0;
    case DiamondLogicalControl::Guide: return (state.buttons & ButtonGuide) != 0;
    case DiamondLogicalControl::DpadUp:
        return (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) != 0;
    case DiamondLogicalControl::DpadDown:
        return (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) != 0;
    case DiamondLogicalControl::DpadLeft:
        return (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) != 0;
    case DiamondLogicalControl::DpadRight:
        return (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) != 0;
    case DiamondLogicalControl::RightStickRight: return x == 1 && y == 0;
    case DiamondLogicalControl::RightStickLeft: return x == -1 && y == 0;
    case DiamondLogicalControl::RightStickUp: return x == 0 && y == -1;
    case DiamondLogicalControl::RightStickDown: return x == 0 && y == 1;
    case DiamondLogicalControl::RightStickUpRight: return x == 1 && y == -1;
    case DiamondLogicalControl::RightStickUpLeft: return x == -1 && y == -1;
    case DiamondLogicalControl::RightStickDownRight: return x == 1 && y == 1;
    case DiamondLogicalControl::RightStickDownLeft: return x == -1 && y == 1;
    case DiamondLogicalControl::None:
    default: return false;
    }
}

void DiamondComboEngine::setControl(
    DiamondLogicalControl control,
    bool down,
    LogicalGamepadState& state
) {
    const auto setButton = [&](std::uint64_t mask) {
        if (down) state.buttons |= mask;
        else state.buttons &= ~mask;
    };
    const auto setDpad = [&](DpadBits bit) {
        const auto mask = static_cast<std::uint8_t>(bit);
        if (down) state.dpad |= mask;
        else state.dpad &= static_cast<std::uint8_t>(~mask);
    };

    switch (control) {
    case DiamondLogicalControl::South: setButton(ButtonSouth); break;
    case DiamondLogicalControl::East: setButton(ButtonEast); break;
    case DiamondLogicalControl::West: setButton(ButtonWest); break;
    case DiamondLogicalControl::North: setButton(ButtonNorth); break;
    case DiamondLogicalControl::LeftBumper: setButton(ButtonLeftBumper); break;
    case DiamondLogicalControl::RightBumper: setButton(ButtonRightBumper); break;
    case DiamondLogicalControl::LeftTrigger: state.leftTrigger = down ? kFullTrigger : 0; break;
    case DiamondLogicalControl::RightTrigger: state.rightTrigger = down ? kFullTrigger : 0; break;
    case DiamondLogicalControl::LeftStickClick: setButton(ButtonLeftStick); break;
    case DiamondLogicalControl::RightStickClick: setButton(ButtonRightStick); break;
    case DiamondLogicalControl::Back: setButton(ButtonBack); break;
    case DiamondLogicalControl::Start: setButton(ButtonStart); break;
    case DiamondLogicalControl::Guide: setButton(ButtonGuide); break;
    case DiamondLogicalControl::DpadUp: setDpad(DpadBits::Up); break;
    case DiamondLogicalControl::DpadDown: setDpad(DpadBits::Down); break;
    case DiamondLogicalControl::DpadLeft: setDpad(DpadBits::Left); break;
    case DiamondLogicalControl::DpadRight: setDpad(DpadBits::Right); break;
    case DiamondLogicalControl::RightStickRight:
        if (down) { state.rx = kAxisMax; state.ry = 0; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickLeft:
        if (down) { state.rx = kAxisMin; state.ry = 0; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickUp:
        if (down) { state.rx = 0; state.ry = kAxisMin; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickDown:
        if (down) { state.rx = 0; state.ry = kAxisMax; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickUpRight:
        if (down) { state.rx = kDiagonal; state.ry = -kDiagonal; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickUpLeft:
        if (down) { state.rx = -kDiagonal; state.ry = -kDiagonal; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickDownRight:
        if (down) { state.rx = kDiagonal; state.ry = kDiagonal; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::RightStickDownLeft:
        if (down) { state.rx = -kDiagonal; state.ry = kDiagonal; }
        else { state.rx = 0; state.ry = 0; }
        break;
    case DiamondLogicalControl::None:
    default: break;
    }
}

std::uint32_t DiamondComboEngine::stepLogicalMask(
    const DiamondComboStep& step
) {
    return step.logicalMask != 0
        ? step.logicalMask
        : controlBit(step.control);
}

void DiamondComboEngine::applyLogicalMask(
    std::uint32_t mask,
    bool down,
    LogicalGamepadState& state,
    const LogicalGamepadState* releasedBase
) {
    // Release generated stick ownership, preserving the player's current input.
    if (!down && releasedBase != nullptr && (mask & kRightStickMask) != 0) {
        state.rx = releasedBase->rx;
        state.ry = releasedBase->ry;
    }
    for (
        std::uint8_t raw = 1;
        raw <= static_cast<std::uint8_t>(DiamondLogicalControl::RightStickDownLeft);
        ++raw
    ) {
        if ((mask & (1u << raw)) != 0) {
            if (down || raw < static_cast<std::uint8_t>(DiamondLogicalControl::RightStickRight)) {
                setControl(static_cast<DiamondLogicalControl>(raw), down, state);
            }
        }
    }
}

void DiamondComboEngine::mergeKey(
    std::array<std::uint8_t, 6>& keys,
    std::uint8_t usage
) {
    if (usage == 0) return;
    for (const auto key : keys) if (key == usage) return;
    for (auto& key : keys) {
        if (key == 0) {
            key = usage;
            return;
        }
    }
}

void DiamondComboEngine::removeKey(
    std::array<std::uint8_t, 6>& keys,
    std::uint8_t usage
) {
    for (auto& key : keys) {
        if (key == usage) key = 0;
    }
}

bool DiamondComboEngine::triggerActive(
    const DiamondComboProgram& program,
    const KeyboardState* keyboard,
    const MouseState* mouse,
    const LogicalGamepadState& state
) {
    for (const auto& trigger : program.triggers) {
        if (!trigger.enabled) continue;

        switch (trigger.kind) {
        case DiamondComboTriggerKind::LogicalControl:
            if (
                trigger.code <= static_cast<std::uint16_t>(
                    DiamondLogicalControl::RightStickDownLeft
                ) &&
                controlActive(
                    static_cast<DiamondLogicalControl>(trigger.code),
                    state
                )
            ) return true;
            break;

        case DiamondComboTriggerKind::KeyboardUsage:
            if (
                keyboard != nullptr &&
                keyboard->connected &&
                (
                    (
                        trigger.code == 0 &&
                        trigger.modifiers != 0 &&
                        (keyboard->modifiers & trigger.modifiers) ==
                            trigger.modifiers
                    ) ||
                    (
                        trigger.code != 0 &&
                        keyboard->pressed(
                            static_cast<std::uint8_t>(trigger.code)
                        ) &&
                        (keyboard->modifiers & trigger.modifiers) ==
                            trigger.modifiers
                    )
                )
            ) return true;
            break;

        case DiamondComboTriggerKind::MouseButton:
            if (
                mouse != nullptr &&
                mouse->connected &&
                (mouse->buttons & trigger.code) != 0
            ) return true;
            break;

        case DiamondComboTriggerKind::MouseWheel:
            if (
                mouse != nullptr &&
                mouse->connected &&
                (
                    (trigger.code == 1 && mouse->wheel > 0) ||
                    (trigger.code == 2 && mouse->wheel < 0)
                )
            ) return true;
            break;
        }
    }
    return false;
}

void DiamondComboEngine::clearLogicalTriggers(
    const DiamondComboProgram& program,
    LogicalGamepadState& state
) {
    for (const auto& trigger : program.triggers) {
        if (
            trigger.enabled &&
            trigger.kind == DiamondComboTriggerKind::LogicalControl &&
            trigger.code <= static_cast<std::uint16_t>(
                DiamondLogicalControl::RightStickDownLeft
            )
        ) {
            setControl(
                static_cast<DiamondLogicalControl>(trigger.code),
                false,
                state
            );
        }
    }
}

void DiamondComboEngine::applyHeld(
    const Runtime& runtime,
    LogicalGamepadState& output
) {
    applyLogicalMask(runtime.heldControls, true, output);
    nativeOutput_.keyboard.modifiers |= runtime.heldModifiers;
    for (const auto key : runtime.heldKeys) {
        if (key != 0) nativeOutput_.keyboard.setPressed(key, true);
    }
    nativeOutput_.mouse.buttons |= runtime.heldMouseButtons;
}

void DiamondComboEngine::applyStepChord(
    const DiamondComboStep& step,
    Runtime& runtime,
    LogicalGamepadState& output,
    bool includeWheel
) {
    applyLogicalMask(stepLogicalMask(step), true, output);
    nativeOutput_.keyboard.modifiers |= step.keyboardModifiers;
    for (const auto key : step.keyboardKeys) {
        if (key != 0) nativeOutput_.keyboard.setPressed(key, true);
    }
    nativeOutput_.mouse.buttons |= step.mouseButtons;

    if (includeWheel && !runtime.wheelSent && step.mouseWheel != 0) {
        nativeOutput_.mouse.wheel += step.mouseWheel;
        runtime.wheelSent = true;
    }
}

void DiamondComboEngine::holdStep(
    const DiamondComboStep& step,
    Runtime& runtime
) {
    runtime.heldControls |= stepLogicalMask(step);
    runtime.heldModifiers |= step.keyboardModifiers;
    for (const auto key : step.keyboardKeys) mergeKey(runtime.heldKeys, key);
    runtime.heldMouseButtons |= step.mouseButtons;
}

void DiamondComboEngine::releaseStep(
    const DiamondComboStep& step,
    Runtime& runtime
) {
    runtime.heldControls &= ~stepLogicalMask(step);
    runtime.heldModifiers &= static_cast<std::uint8_t>(~step.keyboardModifiers);
    for (const auto key : step.keyboardKeys) removeKey(runtime.heldKeys, key);
    runtime.heldMouseButtons &= static_cast<std::uint16_t>(~step.mouseButtons);
}

void DiamondComboEngine::advance(
    const DiamondComboStep& step,
    Runtime& runtime,
    std::uint64_t nowUs
) {
    ++runtime.stepIndex;
    runtime.phaseStartedUs = 0;
    runtime.wheelSent = false;
    runtime.nextStepNotBeforeUs =
        step.delayAfterMs == 0
            ? 0
            : nowUs +
                static_cast<std::uint64_t>(step.delayAfterMs) * 1000ull;
}

void DiamondComboEngine::stop(Runtime& runtime) {
    runtime = {};
}

bool DiamondComboEngine::execute(
    const DiamondComboProgram& program,
    Runtime& runtime,
    const LogicalGamepadState& input,
    LogicalGamepadState& output,
    std::uint64_t nowUs
) {
    const LogicalGamepadState unheldOutput = output;
    applyHeld(runtime, output);

    for (unsigned guard = 0; guard < kDiamondComboSteps + 2; ++guard) {
        // A step advanced in this same frame must also honor its release delay.
        if (runtime.nextStepNotBeforeUs != 0 &&
            nowUs < runtime.nextStepNotBeforeUs) return false;
        runtime.nextStepNotBeforeUs = 0;
        if (runtime.stepIndex >= program.stepCount) {
            if (program.repeat == DiamondComboRepeatMode::AutoRepeat) {
                runtime.stepIndex = 0;
                runtime.phaseStartedUs = 0;
                runtime.nextStepNotBeforeUs = 0;
                runtime.pulseDown = true;
                runtime.pulseCount = 0;
                runtime.wheelSent = false;
                continue;
            }
            // Release only the outputs explicitly configured as
            // "UNTIL COMBO END". Holds configured as "UNTIL RELEASE" remain
            // latched and keep the combo alive until its trigger is lifted.
            applyLogicalMask(runtime.endHeldControls, false, output, &unheldOutput);
            runtime.heldControls &= ~runtime.endHeldControls;
            runtime.heldModifiers &= static_cast<std::uint8_t>(
                ~runtime.endHeldModifiers
            );
            nativeOutput_.keyboard.modifiers &= static_cast<std::uint8_t>(
                ~runtime.endHeldModifiers
            );
            for (const auto key : runtime.endHeldKeys) {
                if (key != 0) {
                    removeKey(runtime.heldKeys, key);
                    nativeOutput_.keyboard.setPressed(key, false);
                }
            }
            runtime.heldMouseButtons &= static_cast<std::uint16_t>(
                ~runtime.endHeldMouseButtons
            );
            nativeOutput_.mouse.buttons &= static_cast<std::uint16_t>(
                ~runtime.endHeldMouseButtons
            );
            runtime.endHeldControls = 0;
            runtime.endHeldModifiers = 0;
            runtime.endHeldKeys = {};
            runtime.endHeldMouseButtons = 0;

            const bool hasLatchedHold =
                runtime.heldControls != 0 ||
                runtime.heldModifiers != 0 ||
                runtime.heldMouseButtons != 0 ||
                runtime.heldKeys[0] != 0 ||
                runtime.heldKeys[1] != 0 ||
                runtime.heldKeys[2] != 0 ||
                runtime.heldKeys[3] != 0 ||
                runtime.heldKeys[4] != 0 ||
                runtime.heldKeys[5] != 0;
            return !hasLatchedHold;
        }

        const auto& step = program.steps[runtime.stepIndex];
        if (!step.enabled) {
            advance(step, runtime, nowUs);
            continue;
        }

        if (runtime.phaseStartedUs == 0) runtime.phaseStartedUs = nowUs;
        const std::uint64_t elapsedUs = nowUs - runtime.phaseStartedUs;
        const std::uint64_t durationUs =
            static_cast<std::uint64_t>(step.durationMs) * 1000ull;

        switch (step.kind) {
        case DiamondComboStepKind::HoldStart:
            holdStep(step, runtime);
            applyHeld(runtime, output);

            // HOLD + N ms: keep the output down for the requested finite
            // duration, then release it before the next visible action.
            if (step.durationMs != 0 && step.intervalMs == 0) {
                if (elapsedUs < durationUs) return false;
                releaseStep(step, runtime);
                applyLogicalMask(stepLogicalMask(step), false, output, &unheldOutput);
                nativeOutput_.keyboard.modifiers &=
                    static_cast<std::uint8_t>(~step.keyboardModifiers);
                for (const auto key : step.keyboardKeys) {
                    if (key != 0) nativeOutput_.keyboard.setPressed(key, false);
                }
                nativeOutput_.mouse.buttons &=
                    static_cast<std::uint16_t>(~step.mouseButtons);
                advance(step, runtime, nowUs);
                continue;
            }

            // 0xFFFF in delayAfterMs is an internal persisted marker used by
            // the mobile editor for HOLD UNTIL COMBO END. It is not a delay.
            if (step.delayAfterMs == kHoldUntilComboEndSentinel) {
                runtime.endHeldControls |= stepLogicalMask(step);
                runtime.endHeldModifiers |= step.keyboardModifiers;
                for (const auto key : step.keyboardKeys) {
                    mergeKey(runtime.endHeldKeys, key);
                }
                runtime.endHeldMouseButtons |= step.mouseButtons;
                ++runtime.stepIndex;
                runtime.phaseStartedUs = 0;
                runtime.wheelSent = false;
                runtime.nextStepNotBeforeUs = 0;
                continue;
            }

            // HOLD UNTIL RELEASE: latch until the program's normal trigger
            // cancellation path stops the combo.
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::HoldEnd:
            releaseStep(step, runtime);
            applyLogicalMask(stepLogicalMask(step) & kRightStickMask,
                false, output, &unheldOutput);
            applyLogicalMask(runtime.heldControls & kRightStickMask, true, output);
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::Wait:
            if (elapsedUs < durationUs) return false;
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::WaitUntilPressed:
            if (!controlActive(step.control, input)) return false;
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::WaitUntilReleased:
            if (controlActive(step.control, input)) return false;
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::Press:
            if (elapsedUs < durationUs) {
                applyStepChord(step, runtime, output, true);
                return false;
            }
            advance(step, runtime, nowUs);
            continue;

        case DiamondComboStepKind::Pulse:
            if (runtime.pulseDown) {
                if (elapsedUs < durationUs) {
                    applyStepChord(step, runtime, output, true);
                    return false;
                }
                runtime.pulseDown = false;
                runtime.phaseStartedUs = nowUs;
                runtime.wheelSent = false;
                ++runtime.pulseCount;
                return false;
            }

            if (
                elapsedUs <
                static_cast<std::uint64_t>(step.intervalMs) * 1000ull
            ) return false;

            if (
                step.repeatCount != 0 &&
                runtime.pulseCount >= step.repeatCount
            ) {
                runtime.pulseDown = true;
                runtime.pulseCount = 0;
                advance(step, runtime, nowUs);
                continue;
            }

            runtime.pulseDown = true;
            runtime.phaseStartedUs = nowUs;
            runtime.wheelSent = false;
            continue;
        }
    }

    return false;
}

LogicalGamepadState DiamondComboEngine::apply(
    const std::array<DiamondComboProgram, kDiamondComboSlots>& programs,
    const KeyboardState* keyboard,
    const MouseState* mouse,
    LogicalGamepadState base,
    std::uint64_t nowUs
) {
    const LogicalGamepadState input = base;
    nativeOutput_ = {};
    nativeOutput_.keyboard.connected = true;
    nativeOutput_.mouse.connected = true;

    for (std::size_t i = 0; i < programs.size(); ++i) {
        const auto& program = programs[i];
        auto& runtime = runtime_[i];

        if (!program.enabled || program.stepCount == 0) {
            stop(runtime);
            runtime.previousTrigger = false;
            continue;
        }

        const bool trigger =
            triggerActive(program, keyboard, mouse, input);
        const bool rising = trigger && !runtime.previousTrigger;

        bool start = false;
        switch (program.activation) {
        case DiamondComboActivationMode::WhileHeld:
            start = trigger && !runtime.active;
            break;
        case DiamondComboActivationMode::PressOnce:
            start = rising && !runtime.active;
            break;
        case DiamondComboActivationMode::Toggle:
            if (rising) {
                if (runtime.active) stop(runtime);
                else start = true;
            }
            break;
        }

        if (
            runtime.active &&
            program.cancelOnTriggerPressAgain &&
            rising
        ) stop(runtime);

        if (
            runtime.active &&
            program.cancelOnTriggerRelease &&
            !trigger &&
            runtime.previousTrigger
        ) stop(runtime);

        if (
            runtime.active &&
            program.cancelControlEnabled &&
            controlActive(program.cancelControl, input)
        ) stop(runtime);

        if (start) {
            stop(runtime);
            runtime.active = true;
            runtime.phaseStartedUs = nowUs;
        }

        if (runtime.active) {
            if (!program.passTriggerThrough) {
                clearLogicalTriggers(program, base);
            }

            if (execute(program, runtime, input, base, nowUs)) {
                stop(runtime);
            }

            base.connected = true;
        }

        runtime.previousTrigger = trigger;
    }

    return base;
}

} // namespace oag
