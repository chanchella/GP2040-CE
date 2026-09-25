#include "oag/mapping/native_km_combo_engine.h"

namespace oag {

bool NativeKmComboEngine::configure(
    std::size_t slot,
    const NativeKmComboRule& rule
) {
    if (slot >= rules_.size()) {
        return false;
    }

    rules_[slot] = rule;
    rules_[slot].enabled = true;
    return true;
}

bool NativeKmComboEngine::disable(std::size_t slot) {
    if (slot >= rules_.size()) {
        return false;
    }

    rules_[slot] = {};
    return true;
}

const NativeKmComboRule* NativeKmComboEngine::rule(
    std::size_t slot
) const {
    return slot < rules_.size() ? &rules_[slot] : nullptr;
}

bool NativeKmComboEngine::matches(
    const NativeKmComboRule& rule,
    const KeyboardState& keyboard,
    const MouseState& mouse
) {
    if (!rule.enabled) {
        return false;
    }

    if (
        (keyboard.modifiers & rule.requiredModifiers) !=
        rule.requiredModifiers
    ) {
        return false;
    }

    for (const std::uint8_t usage : rule.requiredKeys) {
        if (usage != 0 && !keyboard.pressed(usage)) {
            return false;
        }
    }

    if (
        (mouse.buttons & rule.requiredMouseButtons) !=
        rule.requiredMouseButtons
    ) {
        return false;
    }

    return true;
}

NativeKmComboFrame NativeKmComboEngine::apply(
    const KeyboardState& keyboard,
    const MouseState& mouse
) const {
    NativeKmComboFrame frame {
        keyboard,
        mouse,
    };

    for (const NativeKmComboRule& combo : rules_) {
        if (!matches(combo, keyboard, mouse)) {
            continue;
        }

        if (combo.suppressTriggerInputs) {
            frame.keyboard.modifiers &= static_cast<std::uint8_t>(
                ~combo.requiredModifiers
            );

            for (const std::uint8_t usage : combo.requiredKeys) {
                if (usage != 0) {
                    frame.keyboard.setPressed(usage, false);
                }
            }

            frame.mouse.buttons &= static_cast<std::uint16_t>(
                ~combo.requiredMouseButtons
            );
        }

        frame.keyboard.modifiers |= combo.outputModifiers;

        for (const std::uint8_t usage : combo.outputKeys) {
            if (usage != 0) {
                frame.keyboard.setPressed(usage, true);
            }
        }

        frame.mouse.buttons |= combo.outputMouseButtons;
    }

    return frame;
}

} // namespace oag
