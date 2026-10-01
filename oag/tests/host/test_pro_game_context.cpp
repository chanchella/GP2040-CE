#include <cassert>
#include <cstddef>
#include "oag/config/pro_input_config.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/diamond_combo_engine.h"
#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"
#include "oag/mapping/pro_profile_shortcut.h"
#include "oag/output/touch/mobile_touch_mapper.h"

using namespace oag;

// Renaming the V6 flag must preserve existing Flash records byte for byte.
struct PreviousProInputConfig {
    bool nativeDesktop = false;
    std::array<std::uint8_t, 3> reserved {};
    std::array<ProInputSettings, 3> defaults {};
    std::array<ProDeviceProfile, 16> devices {};
};
static_assert(sizeof(PreviousProInputConfig) == sizeof(ProInputConfig));
static_assert(offsetof(ProInputConfig, gameContextInactive) == offsetof(PreviousProInputConfig, nativeDesktop));
static_assert(offsetof(ProInputConfig, defaults) == offsetof(PreviousProInputConfig, defaults));
static_assert(offsetof(ProInputConfig, devices) == offsetof(PreviousProInputConfig, devices));

void samePhysicalOutput(const LogicalGamepadState& a, const LogicalGamepadState& b) {
    assert(a.connected == b.connected && a.buttons == b.buttons && a.dpad == b.dpad);
    assert(a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry);
    assert(a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger);
    assert(a.generation == b.generation && a.timestampUs == b.timestampUs);
}

int main() {
    KeyboardState keyboard {}; keyboard.connected = true;
    keyboard.setPressed(0x1a, true); // physical W
    keyboard.setPressed(0x2c, true); // physical Space / combo trigger
    MouseState mouse {}; mouse.connected = true; mouse.buttons = MouseButtonLeft;
    LogicalGamepadState pad {}; pad.connected = true; pad.buttons = ButtonNorth;
    pad.lx = 123; pad.ry = -456; pad.generation = 7; pad.timestampUs = 99;

    std::array<DiamondComboProgram, kDiamondComboSlots> programs {};
    auto& combo = programs[0]; combo.enabled = true; combo.stepCount = 1;
    combo.activation = DiamondComboActivationMode::WhileHeld;
    combo.triggers[0] = {true, DiamondComboTriggerKind::KeyboardUsage, 0x2c, 0};
    auto& step = combo.steps[0]; step.enabled = true;
    step.kind = DiamondComboStepKind::HoldStart; step.durationMs = 0;
    step.control = DiamondLogicalControl::East;
    step.keyboardKeys[0] = 0x05; step.mouseButtons = MouseButtonMiddle;

    ProProfileShortcut shortcut;
    KeyboardState chord {}; chord.setPressed(0x3a, true); chord.setPressed(0x27, true);
    assert(shortcut.poll(chord, 1).action == ProShortcutAction::None);
    assert(shortcut.poll(chord, 1000001).action == ProShortcutAction::CancelGameProfile);

    // Cancellation works after every selectable game, including repeated
    // digits (11) and the zero-containing game numbers 10 and 20.
    for (unsigned game = 1; game <= 20; ++game) {
        ProProfileShortcut selection;
        KeyboardState command {}; command.setPressed(0x3a, true);
        const auto first = game < 10 ? game : game / 10;
        command.setPressed(0x1d + first, true);
        selection.poll(command, 1);
        if (game >= 10) {
            command.setPressed(0x1d + first, false); selection.poll(command, 5);
            command.setPressed(game % 10 ? 0x1d + game % 10 : 0x27, true);
            selection.poll(command, 10);
        }
        const auto chosen = selection.poll(command, 1000010);
        assert(chosen.action == ProShortcutAction::Game && chosen.number == game);
        selection.poll({}, 1000011);
        selection.poll(chord, 1000012);
        assert(selection.poll(chord, 2000012).action == ProShortcutAction::CancelGameProfile);
    }

    // Exercise the actual combo gate AFTER each physical route. Cancelling
    // must release generated controls without removing the mapped base input.
    KeyboardMouseGamepadMapper mapper;
    for (unsigned route = 0; route < 3; ++route) {
        auto base = route == 0 ? pad : mapper.apply(&keyboard, &mouse, {10, -5}, pad);
        if (route == 2) base.buttons |= kPubgMouseLeftButton;
        DiamondComboEngine effects;
        const auto active = effects.apply(programs, &keyboard, &mouse, base, 1);
        assert(active.buttons & ButtonEast);
        assert(effects.nativeOutput().keyboard.pressed(0x05));
        assert(effects.nativeOutput().mouse.buttons & MouseButtonMiddle);
        const auto cancelled = effects.apply(programs, &keyboard, &mouse, base, 2, false);
        samePhysicalOutput(cancelled, base);
        assert(!effects.active());
        assert(!effects.nativeOutput().keyboard.pressed(0x05));
        assert(effects.nativeOutput().mouse.buttons == 0);
        // A still-held trigger cannot restart the last game's combo.
        samePhysicalOutput(effects.apply(programs, &keyboard, &mouse, base, 100000, false), base);
        assert(keyboard.connected && keyboard.pressed(0x1a) && keyboard.pressed(0x2c));
        assert(mouse.connected && mouse.buttons == MouseButtonLeft && pad.connected);
        if (route == 2) {
            MobileTouchMapper before, after;
            const auto expected = before.map(base), actual = after.map(cancelled);
            assert(actual.count == expected.count && actual.count != 0);
            for (std::size_t i = 0; i < actual.count; ++i) {
                assert(actual.contacts[i].id == expected.contacts[i].id);
                assert(actual.contacts[i].x == expected.contacts[i].x);
                assert(actual.contacts[i].y == expected.contacts[i].y);
            }
        }
        assert(effects.apply(programs, &keyboard, &mouse, base, 100001, true).buttons & ButtonEast);
    }
}
