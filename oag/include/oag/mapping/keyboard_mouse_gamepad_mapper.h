#pragma once

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/digital_binding_engine.h"
#include "oag/mapping/mouse_to_stick_mapper.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

class KeyboardMouseGamepadMapper {
public:
    KeyboardMouseGamepadMapper();

    void loadDefaultFpsProfile();

    LogicalGamepadState apply(
        const KeyboardState* keyboard,
        const MouseState* mouse,
        MouseMotion mouseMotion,
        LogicalGamepadState base = {}
    ) const;

    DigitalBindingEngine& bindings() { return bindings_; }
    const DigitalBindingEngine& bindings() const { return bindings_; }

    MouseStickConfig& mouseConfig() { return mouseConfig_; }
    const MouseStickConfig& mouseConfig() const { return mouseConfig_; }

private:
    DigitalBindingEngine bindings_;
    MouseToStickMapper mouseMapper_;
    MouseStickConfig mouseConfig_ {};
};

} // namespace oag
