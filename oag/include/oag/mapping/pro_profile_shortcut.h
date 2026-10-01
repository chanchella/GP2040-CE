#pragma once
#include "oag/input/keyboard_state.h"
namespace oag {
enum class ProShortcutAction { None, Game, Weapon, Desktop };
struct ProShortcutSelection { ProShortcutAction action = ProShortcutAction::None; std::uint16_t number = 0; };
class ProProfileShortcut {
public:
    ProShortcutSelection poll(const KeyboardState& k, std::uint64_t now) {
        const bool f1 = k.pressed(0x3A), f4 = k.pressed(0x3D), f5 = k.pressed(0x3E);
        const std::uint8_t mode = f1 && !f5 ? 1 : f5 && !f4 && !f1 ? 2 : 0;
        std::uint16_t digits = k.pressed(0x27) ? 1 : 0;
        for (unsigned d = 1; d <= 9; ++d) if (k.pressed(0x1D + d)) digits |= 1u << d;
        if (!f1 && !f5 && digits == 0) maskUntilRelease_ = false;
        if (!mode || mode != mode_) {
            mode_ = mode; value_ = previousDigits_ = 0; started_ = desktop_ = latched_ = false;
        }
        if (!mode) return {};
        if (digits) maskUntilRelease_ = true;
        const auto fresh = digits & ~previousDigits_; previousDigits_ = digits;
        if (latched_) return {};
        if (desktop_ && !(digits & 1)) { desktop_ = started_ = false; }
        if (mode == 1 && value_ == 0 && fresh == 1) {
            desktop_ = started_ = true; startedUs_ = now;
        } else if (fresh && !desktop_) {
            for (unsigned i = 1; i <= 10; ++i) {
                const unsigned d = i % 10;
                if (!(fresh & (1u << d))) continue;
                const unsigned next = value_ * 10 + d;
                if (next >= 1 && next <= (mode == 1 ? 20u : 24u)) {
                    value_ = next; started_ = true; startedUs_ = now;
                }
            }
        }
        if (!started_ || now - startedUs_ < 1000000) return {};
        latched_ = true;
        return {desktop_ ? ProShortcutAction::Desktop : mode == 1 ? ProShortcutAction::Game : ProShortcutAction::Weapon, value_};
    }
    void mask(KeyboardState& k) const {
        if (!maskUntilRelease_) return;
        k.setPressed(0x3A, false);
        if (!k.pressed(0x3D)) k.setPressed(0x3E, false);
        for (unsigned u = 0x1E; u <= 0x27; ++u) k.setPressed(u, false);
    }
private:
    std::uint8_t mode_ = 0;
    std::uint16_t value_ = 0, previousDigits_ = 0;
    std::uint64_t startedUs_ = 0;
    bool started_ = false, desktop_ = false, latched_ = false, maskUntilRelease_ = false;
};
} // namespace oag
