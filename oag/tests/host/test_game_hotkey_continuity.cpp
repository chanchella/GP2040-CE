#include <cassert>
#include <cstdint>
#include <vector>

#include "oag/config/diamond_game_library.h"
#include "oag/firmware/pc_native_km_output.h"
#include "oag/mapping/diamond_combo_engine.h"
#include "oag/mapping/pro_input_processor.h"
#include "oag/mapping/pro_profile_shortcut.h"

namespace {
std::uint64_t nowUs = 1;
bool ready = true;
struct Packet { std::uint8_t instance; std::vector<std::uint8_t> bytes; };
std::vector<Packet> sent;
}
std::uint64_t time_us_64() { return nowUs; }
bool tud_hid_n_ready(std::uint8_t) { return ready; }
bool tud_hid_n_report(std::uint8_t n, std::uint8_t, const void* data, std::uint16_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    sent.push_back({n, {bytes, bytes + size}});
    return true;
}
std::uint8_t tud_hid_n_get_protocol(std::uint8_t) { return 1; }
namespace oag::firmware {
std::uint8_t nativeKeyboardHidInstance() { return 4; }
std::uint8_t nativeMouseHidInstance() { return 5; }
}

struct ConfigStoreSpy {
    oag::DiamondPersistentConfig value {};
    unsigned writes = 0;
    auto& config() { return value; }
    bool save() { ++writes; return true; }
};
struct GameLibrarySpy {
    bool fail = false;
    unsigned activations = 0;
    std::size_t active = 0;
    bool activate(std::size_t game, const oag::DiamondPersistentConfig&) {
        if (fail) return false;
        ++activations;
        active = game;
        return true;
    }
};

struct HotkeyHarness {
    ConfigStoreSpy configStore_;
    GameLibrarySpy gameLibrary_;
    oag::ProProfileShortcut profileShortcut_;
    oag::ProInputProcessor proInput_;
    oag::firmware::PcNativeKmOutput nativeKmOutput_;
    oag::DiamondComboEngine diamondCombos_;
    oag::KeyboardState keys {};
    bool gameContextInactive_ = false, keyboardDirty_ = false, mouseDirty_ = false;
    bool diamondRecoilActive_ = true;
    std::uint64_t nextDiamondComboServiceUs_ = 7, nextDiamondRecoilServiceUs_ = 8, nextNativeRecoilUs_ = 9;
    std::int32_t nativeRecoilAccumX_ = 11, nativeRecoilAccumY_ = 12;
    unsigned compositions = 0;
    oag::KeyboardState combinedKeyboard() const { return keys; }
    void sendComposedOutput() { ++compositions; }

    // Generated from main.cpp, so a storage/output change in the actual
    // firmware is exercised here rather than reimplemented in the test.
    #include "oag_game_hotkey_handlers.inc"

    void select(unsigned number, bool weapon = false) {
        keys.setPressed(weapon ? 0x3e : 0x3a, true);
        const unsigned first = number >= 10 ? number / 10 : number;
        const auto firstUsage = first ? 0x1d + first : 0x27;
        keys.setPressed(firstUsage, true);
        serviceOagGameWeaponHotkey();
        if (number >= 10) {
            keys.setPressed(firstUsage, false);
            ++nowUs;
            serviceOagGameWeaponHotkey();
            const auto last = number % 10;
            keys.setPressed(last ? 0x1d + last : 0x27, true);
            ++nowUs;
            serviceOagGameWeaponHotkey();
        }
        nowUs += 1000000;
        serviceOagGameWeaponHotkey();
        const auto compositionsAfterChoice = compositions;
        nowUs += 1000000;
        serviceOagGameWeaponHotkey();
        assert(compositions == compositionsAfterChoice); // one action per chord
        keys.setPressed(weapon ? 0x3e : 0x3a, false);
        for (unsigned key = 0x1e; key <= 0x27; ++key) keys.setPressed(key, false);
        ++nowUs;
        serviceOagGameWeaponHotkey();
    }
};

std::int16_t axis(const Packet& p, unsigned at) {
    return static_cast<std::int16_t>(p.bytes[at] | std::uint16_t(p.bytes[at + 1]) << 8);
}

int main() {
    for (unsigned game = 1; game <= 20; ++game) {
        sent.clear(); ready = true;
        HotkeyHarness h;
        h.keys.connected = true;
        h.keys.setPressed(0x1a, true); // W remains physically held throughout
        oag::MouseState mouse {}; mouse.connected = true; mouse.buttons = oag::MouseButtonLeft;
        h.nativeKmOutput_.setEnabled(true);
        h.nativeKmOutput_.updateState(h.keys, mouse);
        h.nativeKmOutput_.task(nowUs);
        sent.clear(); ready = false; // host endpoint busy during the selection
        h.nativeKmOutput_.addMouseMotion(317, -211, 2, -1);

        const oag::DeviceId source {0, 1};
        auto settings = h.configStore_.config().proInput.defaults[0];
        settings.automaticMultiplier = false; settings.multiplierPermille = 500;
        h.configStore_.config().proInput.defaults[0] = settings;
        h.proInput_.processMouse(source, {1, 1}, settings);
        assert(h.proInput_.stats(0).remainderX != 0);

        std::array<oag::DiamondComboProgram, oag::kDiamondComboSlots> programs {};
        auto& combo = programs[0]; combo.enabled = true; combo.stepCount = 1;
        combo.triggers[0] = {true, oag::DiamondComboTriggerKind::KeyboardUsage, 0x1a, 0};
        auto& step = combo.steps[0]; step.enabled = true;
        step.kind = oag::DiamondComboStepKind::HoldStart;
        step.control = oag::DiamondLogicalControl::East;
        step.keyboardKeys[0] = 0x05; step.mouseButtons = oag::MouseButtonMiddle;
        h.diamondCombos_.apply(programs, &h.keys, &mouse, {}, nowUs);
        assert(h.diamondCombos_.active());
        h.configStore_.config().runtime.activeWeapon = 3;
        h.select(game);

        assert(h.gameLibrary_.active == game - 1 && h.gameLibrary_.activations == 1);
        assert(h.configStore_.config().runtime.activeGame == game - 1);
        assert(h.configStore_.config().runtime.activeWeapon == oag::kDiamondNoActiveWeapon);
        assert(h.configStore_.writes == 0); // Flash erase must never stop USB/BT here
        assert(!h.gameContextInactive_ && !h.diamondCombos_.active());
        assert(h.diamondCombos_.nativeOutput().mouse.buttons == 0);
        assert(!h.diamondCombos_.nativeOutput().keyboard.pressed(0x05));
        assert(!h.diamondRecoilActive_ && h.nextDiamondComboServiceUs_ == 0);
        assert(h.proInput_.stats(0).remainderX != 0);
        assert(h.keys.connected && h.keys.pressed(0x1a) && mouse.connected);
        assert(h.keyboardDirty_ && h.mouseDirty_);

        ready = true;
        h.nativeKmOutput_.task(++nowUs);
        assert(sent.size() == 1 && sent.back().instance == 5);
        assert(sent.back().bytes[0] == oag::MouseButtonLeft);
        assert(axis(sent.back(), 1) == 317 && axis(sent.back(), 3) == -211);
        h.select(0);
        assert(h.gameContextInactive_ && h.configStore_.writes == 0);
        assert(h.proInput_.processMouse(source, {1, 1}, settings).dx == 1);
        h.keys.setPressed(0x1a, false);
        h.nativeKmOutput_.updateState(h.keys, mouse);
        h.nativeKmOutput_.addMouseMotion(5, 9, 0, 0);
        h.nativeKmOutput_.task(++nowUs);
        assert(sent[sent.size() - 2].instance == 4 && sent[sent.size() - 2].bytes[2] == 0);
        assert(sent.back().instance == 5 && axis(sent.back(), 1) == 5);

        // A failed load leaves the previous game's state and physical output intact.
        h.gameLibrary_.fail = true;
        h.select(game);
        assert(h.gameContextInactive_ && h.gameLibrary_.activations == 1);
        assert(h.configStore_.writes == 0);
    }
}
