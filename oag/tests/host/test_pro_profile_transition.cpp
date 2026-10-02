#include <cassert>
#include <cstdint>
#include <vector>

#include "oag/config/diamond_persistent_config.h"
#include "oag/config/diamond_game_library.h"
#include "oag/firmware/pc_native_km_output.h"
#include "oag/mapping/oag_smart_combo_engine.h"
#include "oag/mapping/oag_auto_input.h"
#include "oag/mapping/keyboard_mouse_gamepad_mapper.h"
#include "oag/mapping/pro_input_processor.h"
#include "oag/mapping/pro_profile_shortcut.h"

namespace {
std::uint64_t nowUs = 0;
bool ready = false;
struct Sent { std::uint8_t instance; std::vector<std::uint8_t> bytes; };
std::vector<Sent> sent;
}
std::uint64_t time_us_64() { return nowUs; }
bool tud_hid_n_ready(std::uint8_t) { return ready; }
bool tud_hid_n_report(std::uint8_t instance, std::uint8_t, const void* raw, std::uint16_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(raw);
    sent.push_back({instance, {bytes, bytes + size}});
    return true;
}
std::uint8_t tud_hid_n_get_protocol(std::uint8_t) { return 1; }
namespace oag::firmware {
std::uint8_t nativeKeyboardHidInstance() { return 4; }
std::uint8_t nativeMouseHidInstance() { return 5; }
}

struct Store {
    oag::DiamondPersistentConfig value;
    unsigned saves = 0;
    auto& config() { return value; }
    bool save() { ++saves; return true; }
};
struct Library {
    bool succeeds = true;
    unsigned activations = 0;
    bool activate(std::size_t index, const oag::DiamondPersistentConfig&) {
        ++activations;
        return succeeds && index < oag::kDiamondLibraryGameSlots;
    }
};

struct CoreHarness {
    Store configStore_;
    Library gameLibrary_;
    oag::ProProfileShortcut profileShortcut_;
    oag::OagSmartComboEngine smartCombos_;
    oag::ProInputProcessor proInput_;
    oag::OagAutoInput autoInput_;
    oag::firmware::PcNativeKmOutput nativeKmOutput_;
    oag::KeyboardState keyboard;
    oag::MouseState mouse;
    oag::MouseMotion currentMouseMotion_ {321, -678};
    bool gameContextInactive_ = false, keyboardDirty_ = false, mouseDirty_ = false;
    bool diamondRecoilActive_ = true;
    std::uint64_t nextDiamondComboServiceUs_ = 17, nextDiamondRecoilServiceUs_ = 19, nextNativeRecoilUs_ = 23;
    std::int32_t nativeRecoilAccumX_ = 7, nativeRecoilAccumY_ = -8;
    unsigned keyboardMouseMode_ = 0;
    oag::LogicalGamepadState output;
    unsigned composed = 0;
    static constexpr std::uint8_t kOagF1Usage = 0x3a, kModeToggleF4Usage = 0x3d, kModeToggleF5Usage = 0x3e;
    auto combinedKeyboard() const { return keyboard; }
    unsigned smartResets=0,smartActivations=0;
    void resetOagSmartEffects() { ++smartResets; smartCombos_.cancel(); }
    void resetOagSmartWeaponEffects() {}
    void activateOagSmartGame(std::size_t) { ++smartActivations; }

    void sendComposedOutput() {
        ++composed;
        auto keys = keyboard;
        maskOagGameWeaponHotkey(keys);
        if (keyboardMouseMode_ != 0) {
            output = oag::KeyboardMouseGamepadMapper {}.apply(&keys, &mouse, currentMouseMotion_, {});
        }
        nativeKmOutput_.updateState(keys, mouse);
    }

    // These are the actual FirmwareCore methods, regenerated on every source
    // change. Hardware/Flash are stubbed; native HID output and fractions are real.
#include "profile_transition_under_test.inc"
};

void select(CoreHarness& h, unsigned game, std::uint64_t start) {
    h.keyboard.setPressed(0x3a, true);
    const unsigned first = game < 10 ? game : game / 10;
    h.keyboard.setPressed(0x1d + first, true);
    nowUs = start;
    h.serviceOagGameWeaponHotkey();
    if (game >= 10) {
        h.keyboard.setPressed(0x1d + first, false);
        nowUs = start + 1;
        h.serviceOagGameWeaponHotkey();
        h.keyboard.setPressed(game % 10 ? 0x1d + game % 10 : 0x27, true);
        nowUs = start + 2;
        h.serviceOagGameWeaponHotkey();
    }
    nowUs = start + 1000002;
    h.serviceOagGameWeaponHotkey();
}
void releaseChord(CoreHarness& h) {
    h.keyboard.setPressed(0x3a, false);
    for (unsigned u = 0x1e; u <= 0x27; ++u) h.keyboard.setPressed(u, false);
    ++nowUs;
    h.serviceOagGameWeaponHotkey();
}
void cancel(CoreHarness& h) {
    releaseChord(h);
    h.keyboard.setPressed(0x3a, true);
    h.keyboard.setPressed(0x27, true);
    ++nowUs;
    h.serviceOagGameWeaponHotkey();
    nowUs += 1000000;
    h.serviceOagGameWeaponHotkey();
}
std::int16_t axis(const Sent& report, unsigned offset) {
    return static_cast<std::int16_t>(report.bytes[offset] | std::uint16_t(report.bytes[offset + 1]) << 8);
}

int main() {
    for (unsigned route = 0; route < 3; ++route) {
        for (unsigned game = 1; game <= 20; ++game) {
            CoreHarness h;
            h.keyboardMouseMode_ = route;
            h.keyboard.connected = h.mouse.connected = true;
            h.keyboard.setPressed(0x04, true); // A stays down through the command.
            h.mouse.buttons = oag::MouseButtonLeft;
            h.nativeKmOutput_.setEnabled(true);

            // An old game's generated B/middle-button hold must disappear
            // while the physical A/left-button hold continues without a gap.
            std::array<oag::OagSmartCombo,oag::kDiamondComboSlots> oldPrograms {};
            auto& old=oldPrograms[0]; old.enabled=1; old.mode=oag::OagExecution::WhileHeld;
            auto& branch=old.branches[0]; branch.conditions[0].control={oag::OagSource::Keyboard,0,0x04};
            branch.conditions[0].kind=oag::OagTrigger::Held; branch.thenCount=3;
            branch.actions[0].control={oag::OagSource::Gamepad,0,2};
            branch.actions[1].control={oag::OagSource::Keyboard,0,0x05};
            branch.actions[2].control={oag::OagSource::Mouse,0,2};
            for (auto& a:branch.actions) a.kind=oag::OagActionKind::Press;
            h.smartCombos_.configure(oldPrograms);
            h.smartCombos_.tick({&h.keyboard,&h.mouse,{}},1,true);
            assert(h.smartCombos_.active());
            auto generatedKeys = h.keyboard;
            generatedKeys.setPressed(0x05, true);
            auto generatedMouse = h.mouse;
            generatedMouse.buttons |= oag::MouseButtonMiddle;
            h.nativeKmOutput_.updateState(generatedKeys, generatedMouse);
            ready = true;
            h.nativeKmOutput_.task(1);
            h.nativeKmOutput_.addMouseMotion(321, -678, 2, -1);

            oag::ProInputSettings half;
            half.automaticMultiplier = false;
            half.multiplierPermille = 500;
            h.proInput_.processMouse({0, 7}, {1, -1}, half);
            const auto before = h.proInput_.stats(0);
            h.autoInput_.mouse({0,7},{8,-4},1);
            sent.clear(); ready = false;

            select(h, game, 1);
            assert(h.configStore_.value.runtime.activeGame == game - 1);
            assert(!h.gameContextInactive_ && !h.configStore_.value.proInput.gameContextInactive);
            assert(h.configStore_.saves == 0 && h.composed == 1);
            assert(h.keyboardDirty_ && h.mouseDirty_ && !h.diamondRecoilActive_);
            assert(!h.smartCombos_.active());
            assert(!h.smartCombos_.output().keyboard.pressed(0x05));
            assert(h.smartCombos_.output().mouse.buttons == 0);
            assert(h.keyboardMouseMode_ == route);
            assert(h.currentMouseMotion_.dx == 321 && h.currentMouseMotion_.dy == -678);
            assert(h.proInput_.stats(0).remainderX == before.remainderX);
            assert(h.proInput_.stats(0).remainderY == before.remainderY);
            const auto tail=h.autoInput_.flush({0,7},nowUs);assert(tail.dx==2&&tail.dy==-1);
            h.nativeKmOutput_.task(nowUs);
            assert(sent.empty());

            // A selection must not insert neutral reports or lose buffered motion
            // when USB becomes ready. Both physical controls remain pressed.
            ready = true;
            h.nativeKmOutput_.task(++nowUs);
            assert(sent.size() == 2);
            assert(sent[0].instance == 4 && sent[0].bytes[2] == 0x04);
            assert(sent[1].instance == 5 && sent[1].bytes[0] == oag::MouseButtonLeft);
            assert(axis(sent[1], 1) == 321 && axis(sent[1], 3) == -678);

            ready = false;
            h.nativeKmOutput_.addMouseMotion(200, -250, 0, 0);
            cancel(h);
            assert(h.gameContextInactive_ && h.configStore_.value.proInput.gameContextInactive);
            assert(h.configStore_.value.runtime.activeWeapon == oag::kDiamondNoActiveWeapon);
            assert(h.configStore_.saves == 0 && h.keyboardMouseMode_ == route);
            assert(h.keyboard.connected && h.mouse.connected && h.keyboard.pressed(0x04));
            assert(h.proInput_.stats(0).remainderX == before.remainderX);
            assert(h.proInput_.stats(0).remainderY == before.remainderY);
            const auto tail=h.autoInput_.flush({0,7},nowUs);assert(tail.dx==2&&tail.dy==-1);
            ready = true;
            sent.clear();
            h.nativeKmOutput_.task(++nowUs);
            assert(sent.size() == 1 && sent[0].instance == 5);
            assert(sent[0].bytes[0] == oag::MouseButtonLeft);
            assert(axis(sent[0], 1) == 200 && axis(sent[0], 3) == -250);

            // Re-select while the physical controls remain down.
            releaseChord(h);
            select(h, game, nowUs + 1);
            assert(!h.gameContextInactive_ && h.configStore_.saves == 0);
            assert(h.keyboardMouseMode_ == route && h.keyboard.pressed(0x04));
        }
    }

    CoreHarness failed;
    failed.gameLibrary_.succeeds = false;
    failed.gameContextInactive_ = true;
    failed.configStore_.value.runtime.activeGame = 7;
    select(failed, 1, 1);
    assert(failed.gameContextInactive_ && failed.configStore_.value.runtime.activeGame == 7);
    assert(failed.configStore_.saves == 0 && failed.composed == 0);
}
