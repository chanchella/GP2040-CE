#include "oag/mapping/digital_binding_engine.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

struct DirectionAccumulator {
    bool leftUp = false;
    bool leftDown = false;
    bool leftLeft = false;
    bool leftRight = false;

    bool rightUp = false;
    bool rightDown = false;
    bool rightLeft = false;
    bool rightRight = false;
};

void applyDigitalTarget(
    LogicalDigitalControl target,
    LogicalGamepadState& output,
    DirectionAccumulator& directions
) {
    switch (target) {
        case LogicalDigitalControl::South:
            output.buttons |= ButtonSouth;
            break;
        case LogicalDigitalControl::East:
            output.buttons |= ButtonEast;
            break;
        case LogicalDigitalControl::West:
            output.buttons |= ButtonWest;
            break;
        case LogicalDigitalControl::North:
            output.buttons |= ButtonNorth;
            break;
        case LogicalDigitalControl::LeftBumper:
            output.buttons |= ButtonLeftBumper;
            break;
        case LogicalDigitalControl::RightBumper:
            output.buttons |= ButtonRightBumper;
            break;
        case LogicalDigitalControl::LeftStickClick:
            output.buttons |= ButtonLeftStick;
            break;
        case LogicalDigitalControl::RightStickClick:
            output.buttons |= ButtonRightStick;
            break;
        case LogicalDigitalControl::Back:
            output.buttons |= ButtonBack;
            break;
        case LogicalDigitalControl::Start:
            output.buttons |= ButtonStart;
            break;
        case LogicalDigitalControl::Guide:
            output.buttons |= ButtonGuide;
            break;

        case LogicalDigitalControl::DpadUp:
            output.dpad |= static_cast<std::uint8_t>(DpadBits::Up);
            break;
        case LogicalDigitalControl::DpadDown:
            output.dpad |= static_cast<std::uint8_t>(DpadBits::Down);
            break;
        case LogicalDigitalControl::DpadLeft:
            output.dpad |= static_cast<std::uint8_t>(DpadBits::Left);
            break;
        case LogicalDigitalControl::DpadRight:
            output.dpad |= static_cast<std::uint8_t>(DpadBits::Right);
            break;

        case LogicalDigitalControl::LeftStickUp:
            directions.leftUp = true;
            break;
        case LogicalDigitalControl::LeftStickDown:
            directions.leftDown = true;
            break;
        case LogicalDigitalControl::LeftStickLeft:
            directions.leftLeft = true;
            break;
        case LogicalDigitalControl::LeftStickRight:
            directions.leftRight = true;
            break;

        case LogicalDigitalControl::RightStickUp:
            directions.rightUp = true;
            break;
        case LogicalDigitalControl::RightStickDown:
            directions.rightDown = true;
            break;
        case LogicalDigitalControl::RightStickLeft:
            directions.rightLeft = true;
            break;
        case LogicalDigitalControl::RightStickRight:
            directions.rightRight = true;
            break;

        case LogicalDigitalControl::LeftTrigger:
            output.leftTrigger =
                std::numeric_limits<std::uint32_t>::max();
            break;
        case LogicalDigitalControl::RightTrigger:
            output.rightTrigger =
                std::numeric_limits<std::uint32_t>::max();
            break;
    }
}

std::int32_t resolveAxis(
    bool negative,
    bool positive,
    std::int32_t base
) {
    if (negative && positive) {
        return 0;
    }

    if (negative) {
        return std::numeric_limits<std::int32_t>::min();
    }

    if (positive) {
        return std::numeric_limits<std::int32_t>::max();
    }

    return base;
}

} // namespace

bool DigitalBindingEngine::addBinding(
    BindingSource source,
    LogicalDigitalControl target
) {
    for (const DigitalBinding& binding : bindings_) {
        if (binding.used &&
            binding.source == source &&
            binding.target == target) {
            return true;
        }
    }

    for (DigitalBinding& binding : bindings_) {
        if (!binding.used) {
            binding.used = true;
            binding.source = source;
            binding.target = target;
            return true;
        }
    }

    return false;
}

bool DigitalBindingEngine::removeBinding(
    BindingSource source,
    LogicalDigitalControl target
) {
    for (DigitalBinding& binding : bindings_) {
        if (binding.used &&
            binding.source == source &&
            binding.target == target) {
            binding = {};
            return true;
        }
    }

    return false;
}

std::size_t DigitalBindingEngine::removeSource(
    BindingSource source
) {
    std::size_t removed = 0;

    for (DigitalBinding& binding : bindings_) {
        if (binding.used && binding.source == source) {
            binding = {};
            ++removed;
        }
    }

    return removed;
}

void DigitalBindingEngine::clear() {
    bindings_ = {};
}

std::size_t DigitalBindingEngine::count() const {
    std::size_t total = 0;

    for (const DigitalBinding& binding : bindings_) {
        if (binding.used) {
            ++total;
        }
    }

    return total;
}

bool DigitalBindingEngine::sourceActive(
    BindingSource source,
    const KeyboardState* keyboard,
    const MouseState* mouse
) const {
    switch (source.kind) {
        case BindingSourceKind::KeyboardUsage:
            return keyboard != nullptr &&
                keyboard->connected &&
                source.code < KeyboardState::kUsageCount &&
                keyboard->pressed(
                    static_cast<std::uint8_t>(source.code)
                );

        case BindingSourceKind::MouseButton:
            return mouse != nullptr &&
                mouse->connected &&
                source.code != 0 &&
                (mouse->buttons & source.code) != 0;
    }

    return false;
}

LogicalGamepadState DigitalBindingEngine::apply(
    const KeyboardState* keyboard,
    const MouseState* mouse,
    LogicalGamepadState base
) const {
    DirectionAccumulator directions {};

    for (const DigitalBinding& binding : bindings_) {
        if (!binding.used ||
            !sourceActive(binding.source, keyboard, mouse)) {
            continue;
        }

        applyDigitalTarget(
            binding.target,
            base,
            directions
        );
    }

    base.lx = resolveAxis(
        directions.leftLeft,
        directions.leftRight,
        base.lx
    );

    base.ly = resolveAxis(
        directions.leftUp,
        directions.leftDown,
        base.ly
    );

    base.rx = resolveAxis(
        directions.rightLeft,
        directions.rightRight,
        base.rx
    );

    base.ry = resolveAxis(
        directions.rightUp,
        directions.rightDown,
        base.ry
    );

    return base;
}

} // namespace oag
