#include "oag/mapping/diamond_combo_engine.h"

#include "oag/input/gamepad_state.h"

namespace {

constexpr std::uint32_t kFullTrigger = 0xFFFFu;

constexpr std::uint32_t controlBit(oag::DiamondLogicalControl control) {
    const auto raw = static_cast<std::uint8_t>(control);
    return raw == 0 || raw >= 32 ? 0u : (1u << raw);
}

} // namespace

namespace oag {

void DiamondComboEngine::reset() {
    runtime_ = {};
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
    case DiamondLogicalControl::None:
    default:
        return false;
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
    case DiamondLogicalControl::None:
    default:
        break;
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
                    DiamondLogicalControl::DpadRight
                ) &&
                controlActive(
                    static_cast<DiamondLogicalControl>(trigger.code),
                    state
                )
            ) {
                return true;
            }
            break;

        case DiamondComboTriggerKind::KeyboardUsage:
            if (
                keyboard != nullptr &&
                keyboard->connected &&
                keyboard->pressed(static_cast<std::uint8_t>(trigger.code)) &&
                (keyboard->modifiers & trigger.modifiers) == trigger.modifiers
            ) {
                return true;
            }
            break;

        case DiamondComboTriggerKind::MouseButton:
            if (
                mouse != nullptr &&
                mouse->connected &&
                (mouse->buttons & trigger.code) != 0
            ) {
                return true;
            }
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
                DiamondLogicalControl::DpadRight
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

void DiamondComboEngine::stop(Runtime& runtime) {
    runtime.active = false;
    runtime.stepIndex = 0;
    runtime.phaseStartedUs = 0;
    runtime.pulseDown = true;
    runtime.pulseCount = 0;
    runtime.heldControls = 0;
}

bool DiamondComboEngine::execute(
    const DiamondComboProgram& program,
    Runtime& runtime,
    const LogicalGamepadState& input,
    LogicalGamepadState& output,
    std::uint64_t nowUs
) {
    for (std::uint8_t raw = 1;
         raw <= static_cast<std::uint8_t>(DiamondLogicalControl::DpadRight);
         ++raw) {
        if ((runtime.heldControls & (1u << raw)) != 0) {
            setControl(static_cast<DiamondLogicalControl>(raw), true, output);
        }
    }

    for (unsigned guard = 0; guard < kDiamondComboSteps + 2; ++guard) {
        if (runtime.stepIndex >= program.stepCount) {
            if (program.repeat == DiamondComboRepeatMode::AutoRepeat) {
                runtime.stepIndex = 0;
                runtime.phaseStartedUs = nowUs;
                runtime.pulseDown = true;
                runtime.pulseCount = 0;
                continue;
            }
            return true;
        }

        const auto& step = program.steps[runtime.stepIndex];
        if (!step.enabled) {
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;
        }

        if (runtime.phaseStartedUs == 0) runtime.phaseStartedUs = nowUs;
        const std::uint64_t elapsedUs = nowUs - runtime.phaseStartedUs;
        const std::uint64_t durationUs =
            static_cast<std::uint64_t>(step.durationMs) * 1000ull;

        switch (step.kind) {
        case DiamondComboStepKind::HoldStart:
            runtime.heldControls |= controlBit(step.control);
            setControl(step.control, true, output);
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::HoldEnd:
            runtime.heldControls &= ~controlBit(step.control);
            setControl(step.control, false, output);
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::Wait:
            if (elapsedUs < durationUs) return false;
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::WaitUntilPressed:
            if (!controlActive(step.control, input)) return false;
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::WaitUntilReleased:
            if (controlActive(step.control, input)) return false;
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::Press:
            if (elapsedUs < durationUs) {
                setControl(step.control, true, output);
                return false;
            }
            ++runtime.stepIndex;
            runtime.phaseStartedUs = nowUs;
            continue;

        case DiamondComboStepKind::Pulse: {
            if (runtime.pulseDown) {
                if (elapsedUs < durationUs) {
                    setControl(step.control, true, output);
                    return false;
                }
                runtime.pulseDown = false;
                runtime.phaseStartedUs = nowUs;
                ++runtime.pulseCount;
                return false;
            }

            const std::uint64_t intervalUs =
                static_cast<std::uint64_t>(step.intervalMs) * 1000ull;
            if (elapsedUs < intervalUs) return false;

            if (
                step.repeatCount != 0 &&
                runtime.pulseCount >= step.repeatCount
            ) {
                ++runtime.stepIndex;
                runtime.phaseStartedUs = nowUs;
                runtime.pulseDown = true;
                runtime.pulseCount = 0;
                continue;
            }

            runtime.pulseDown = true;
            runtime.phaseStartedUs = nowUs;
            continue;
        }
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
        ) {
            stop(runtime);
        }

        if (
            runtime.active &&
            program.cancelOnTriggerRelease &&
            !trigger &&
            runtime.previousTrigger
        ) {
            stop(runtime);
        }

        if (
            runtime.active &&
            program.cancelControlEnabled &&
            controlActive(program.cancelControl, input)
        ) {
            stop(runtime);
        }

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
