#include "oag/mapping/smart_combo_controls.h"
#include "oag/input/gamepad_state.h"
#include <limits>
namespace oag {
namespace {
constexpr std::uint64_t button(std::uint16_t c) {
    switch (c) {
    case 1: return ButtonSouth; case 2: return ButtonEast; case 3: return ButtonWest; case 4: return ButtonNorth;
    case 5: return ButtonLeftBumper; case 6: return ButtonRightBumper; case 9: return ButtonLeftStick; case 10: return ButtonRightStick;
    case 11: return ButtonBack; case 12: return ButtonStart; case 13: return ButtonGuide; case 34: return ButtonShare;
    default: return 0;
    }
}
constexpr std::uint8_t dpad(std::uint16_t c) { return c >= 14 && c <= 17 ? (1u << (c - 14)) : 0; }
std::int32_t axis(std::int16_t value) { return static_cast<std::int32_t>(std::int64_t(value) * std::numeric_limits<std::int32_t>::max() / 1000); }
void directions(std::uint16_t c, int& x, int& y) {
    const auto d = (c >= 26 ? c - 26 : c - 18);
    static constexpr int xs[] = {1,-1,0,0,1,-1,1,-1};
    static constexpr int ys[] = {0,0,-1,1,-1,-1,1,1};
    x = d < 8 ? xs[d] : 0; y = d < 8 ? ys[d] : 0;
}
}
bool oagSmartSame(const OagSmartTarget& a, const OagSmartTarget& b) { return a.kind == b.kind && a.code == b.code; }
bool oagSmartDown(const OagSmartTarget& t, const OagSmartInput& in, std::uint16_t threshold) {
    switch (t.kind) {
    case OagSmartTargetKind::Key:
        return in.keyboard.connected && (t.code >= 224 && t.code <= 231 ? (in.keyboard.modifiers & (1u << (t.code - 224))) != 0 : in.keyboard.pressed(static_cast<std::uint8_t>(t.code)));
    case OagSmartTargetKind::Mouse: return in.mouse.connected && (in.mouse.buttons & t.code);
    case OagSmartTargetKind::Wheel: return in.mouse.connected && (t.code == 1 ? in.mouse.wheel > 0 : in.mouse.wheel < 0);
    case OagSmartTargetKind::Pad: break;
    }
    if (!in.pad.connected) return false;
    if (auto b = button(t.code)) return (in.pad.buttons & b) != 0;
    if (auto d = dpad(t.code)) return (in.pad.dpad & d) != 0;
    if (t.code == 7 || t.code == 8) return (t.code == 7 ? in.pad.leftTrigger : in.pad.rightTrigger) >= (65535u * threshold / 1000u);
    if (t.code >= 18 && t.code <= 33) {
        const auto tx = std::int64_t(std::numeric_limits<std::int32_t>::max()) * threshold / 1000;
        const auto x = t.code >= 26 ? in.pad.lx : in.pad.rx, y = t.code >= 26 ? in.pad.ly : in.pad.ry;
        const int sx = x > tx ? 1 : (x < -tx ? -1 : 0), sy = y > tx ? 1 : (y < -tx ? -1 : 0);
        int dx = 0, dy = 0; directions(t.code, dx, dy); return sx == dx && sy == dy;
    }
    return false;
}
void oagSmartSet(const OagSmartTarget& t, bool down, OagSmartOutput& out) {
    if (t.kind == OagSmartTargetKind::Key) {
        out.keyboard.connected = true;
        if (t.code >= 224 && t.code <= 231) { const auto b = static_cast<std::uint8_t>(1u << (t.code - 224)); if (down) out.keyboard.modifiers |= b; else out.keyboard.modifiers &= ~b; }
        else out.keyboard.setPressed(static_cast<std::uint8_t>(t.code), down);
        return;
    }
    if (t.kind == OagSmartTargetKind::Mouse) { out.mouse.connected = true; if (down) out.mouse.buttons |= t.code; else out.mouse.buttons &= ~t.code; return; }
    if (t.kind == OagSmartTargetKind::Wheel) { out.mouse.connected = true; if (down) out.mouse.wheel += t.code == 1 ? 1 : -1; return; }
    out.pad.connected = true;
    if (auto b = button(t.code)) { if (down) out.pad.buttons |= b; else out.pad.buttons &= ~b; return; }
    if (auto d = dpad(t.code)) { if (down) out.pad.dpad |= d; else out.pad.dpad &= ~d; return; }
    if (t.code == 7 || t.code == 8) {
        auto& v = t.code == 7 ? out.pad.leftTrigger : out.pad.rightTrigger; const auto flag = t.code == 7 ? 4u : 8u;
        v = down ? 65535u * t.strength / 100u : 0; if (down) out.axes |= flag; else out.axes &= ~flag; return;
    }
    if (t.code >= 18 && t.code <= 33) {
        int x = 0, y = 0; directions(t.code, x, y);
        auto& ax = t.code >= 26 ? out.pad.lx : out.pad.rx; auto& ay = t.code >= 26 ? out.pad.ly : out.pad.ry;
        const auto strength = static_cast<std::int16_t>((x && y ? 707 : 1000) * t.strength / 100);
        ax = down ? axis(static_cast<std::int16_t>(x * strength)) : 0; ay = down ? axis(static_cast<std::int16_t>(y * strength)) : 0;
        const auto flag = t.code >= 26 ? 1u : 2u; if (down) out.axes |= flag; else out.axes &= ~flag;
    }
}
void oagSmartCompose(const OagSmartOutput& a, LogicalGamepadState& p) {
    p.connected |= a.pad.connected; p.buttons |= a.pad.buttons; p.dpad |= a.pad.dpad;
    if (a.axes & 1) { p.lx = a.pad.lx; p.ly = a.pad.ly; }
    if (a.axes & 2) { p.rx = a.pad.rx; p.ry = a.pad.ry; }
    if (a.axes & 4) p.leftTrigger = a.pad.leftTrigger;
    if (a.axes & 8) p.rightTrigger = a.pad.rightTrigger;
}
void oagSmartMerge(const OagSmartOutput& a, OagSmartOutput& b) {
    oagSmartCompose(a, b.pad); b.axes |= a.axes;
    b.keyboard.connected |= a.keyboard.connected; b.keyboard.modifiers |= a.keyboard.modifiers;
    for (std::size_t i = 0; i < KeyboardState::kWordCount; ++i) b.keyboard.usages[i] |= a.keyboard.usages[i];
    b.mouse.connected |= a.mouse.connected; b.mouse.buttons |= a.mouse.buttons; b.mouse.wheel += a.mouse.wheel;
}
void oagSmartConsume(const OagSmartTarget& t, OagSmartInput& in) {
    OagSmartOutput value; oagSmartSet(t, true, value);
    in.pad.buttons &= ~value.pad.buttons; in.pad.dpad &= ~value.pad.dpad;
    if (value.axes & 1) in.pad.lx = in.pad.ly = 0;
    if (value.axes & 2) in.pad.rx = in.pad.ry = 0;
    if (value.axes & 4) in.pad.leftTrigger = 0;
    if (value.axes & 8) in.pad.rightTrigger = 0;
    in.keyboard.modifiers &= ~value.keyboard.modifiers;
    for (std::size_t i = 0; i < KeyboardState::kWordCount; ++i) in.keyboard.usages[i] &= ~value.keyboard.usages[i];
    in.mouse.buttons &= ~value.mouse.buttons;
    if (t.kind == OagSmartTargetKind::Wheel) in.mouse.wheel = 0;
}
} // namespace oag
