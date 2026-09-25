#pragma once

#include <array>
#include <cstddef>

#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"
#include "oag/mapping/digital_binding_engine.h"
#include "oag/mapping/mouse_to_stick_mapper.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

struct ExtraBindSlot {
    bool enabled = false;
    BindingSource source {};
    LogicalDigitalControl target = LogicalDigitalControl::South;
};

class KeyboardMouseGamepadMapper {
public:
    static constexpr std::size_t kExtraBindSlots = 6;

    KeyboardMouseGamepadMapper();

    void loadDefaultFpsProfile();

    // Six user-configurable primary-controller bind slots. These do not add
    // non-standard XInput buttons; they let extra keyboard/mouse inputs drive
    // any existing logical controller action while keeping full XInput game
    // compatibility.
    bool configureExtraBind(
        std::size_t slot,
        BindingSource source,
        LogicalDigitalControl target
    );

    bool disableExtraBind(std::size_t slot);

    const ExtraBindSlot* extraBind(std::size_t slot) const;

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
    bool rebuildExtraBindings();

    DigitalBindingEngine bindings_;
    DigitalBindingEngine extraBindings_;
    std::array<ExtraBindSlot, kExtraBindSlots> extraBindSlots_ {};
    MouseToStickMapper mouseMapper_;
    MouseStickConfig mouseConfig_ {};
};

} // namespace oag
