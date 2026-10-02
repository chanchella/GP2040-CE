#include "oag/config/oag_smart_config.h"
#include <algorithm>
#include <cstring>

namespace oag {
bool oagValidControl(OagControl c, bool output) {
    if (c.reserved) return false;
    switch (c.source) {
    case OagSource::Gamepad: return c.code >= 1 && c.code <= 17;
    case OagSource::Keyboard: return c.code >= 4 && c.code <= 231;
    case OagSource::Mouse: return c.code < 16;
    case OagSource::Wheel: return c.code >= 1 && c.code <= 4;
    case OagSource::Stick: return c.code < 16; // Left 0..7, right 8..15.
    case OagSource::Axis: return c.code < (output ? 2 : 6);
    }
    return false;
}
bool oagValidateCombo(const OagSmartCombo& c) {
    if (c.enabled > 1 || c.cancelable > 1 || c.branchCount < 1 ||
        c.branchCount > kOagSmartBranches || unsigned(c.mode) > 5 ||
        !std::memchr(c.name.data(), 0, c.name.size())) return false;
    for (std::size_t b = 0; b < c.branchCount; ++b) {
        const auto& r = c.branches[b];
        if (r.enabled > 1 || r.conditionCount < 1 || r.conditionCount > kOagSmartConditions ||
            r.refCount > kOagSmartRefs || r.thenCount + r.elseCount > kOagSmartActions ||
            (c.enabled && r.enabled && !r.thenCount) || r.reserved || r.flags) return false;
        if (!c.enabled && !r.thenCount && !r.elseCount) continue;
        for (std::size_t q = 0; q < r.refCount; ++q)
            if (!oagValidControl(r.refs[q])) return false;
        for (std::size_t i = 0; i < r.conditionCount; ++i) {
            const auto& x = r.conditions[i];
            if (!oagValidControl(x.control) || unsigned(x.kind) > 11 ||
                unsigned(x.join) > 1 || x.negate > 1 || (i == 0 && x.negate) ||
                x.windowMs < 1 || x.windowMs > 60000 || x.holdMs < 1 ||
                x.holdMs > 60000 || x.threshold < -1000 || x.threshold > 1000 ||
                (x.kind == OagTrigger::Multi && (x.taps < 1 || x.taps > 20))) return false;
            if (x.kind == OagTrigger::Chord || x.kind == OagTrigger::Sequence) {
                if (x.refCount < 2 || x.refFirst + x.refCount > r.refCount ||
                    !(r.refs[x.refFirst] == x.control)) return false;
            }
        }
        for (std::size_t i = 0; i < r.thenCount + r.elseCount; ++i) {
            const auto& x = r.actions[i];
            if (unsigned(x.kind) > 13 || x.durationMs > 60000 || x.beforeMs > 60000 ||
                x.afterMs > 60000 || x.intervalMs > 60000 || x.count > 100 ||
                x.x < -1000 || x.x > 1000 || x.y < -1000 || x.y > 1000 || x.flags) return false;
            const auto k = x.kind;
            if (k == OagActionKind::Repeat || k == OagActionKind::Loop) {
                const auto start = i < r.thenCount ? 0u : r.thenCount;
                if (x.first < start || x.first >= i ||
                    (k == OagActionKind::Repeat && !x.count)) return false;
            } else if (k == OagActionKind::MultiPress) {
                if (x.count < 2 || x.first + x.count > r.refCount) return false;
                for (std::size_t j = x.first; j < x.first + x.count; ++j)
                    if (!oagValidControl(r.refs[j], true)) return false;
            } else if (k != OagActionKind::Wait && k != OagActionKind::ReleaseAll &&
                       k != OagActionKind::ReloadWait) {
                if (!oagValidControl(x.control, true)) return false;
                if (k == OagActionKind::StickDirection && x.control.source != OagSource::Stick) return false;
                if (k == OagActionKind::StickValue && x.control.source != OagSource::Axis) return false;
                if (k == OagActionKind::TriggerValue && (x.control.source != OagSource::Gamepad ||
                    (x.control.code != 7 && x.control.code != 8) || x.x < 0)) return false;
            }
        }
    }
    return true;
}
bool oagValidateWeapon(const OagWeaponSettings& w) {
    const auto raw = [](std::int16_t n) { return n >= -200 && n <= 200; };
    if (w.configured > 1 || w.enabled > 1 || w.syncRpm > 1 || w.adsOverride > 1 ||
        w.firstShot > 1 || w.antiShake > 100 || w.smoothing > 100 || !w.tickMs ||
        w.tickMs > 1000 || w.rpm < 60 || w.rpm > 6000 || w.reloadMs > 60000 ||
        w.startDelayMs > 60000 || w.rampMs > 60000 || unsigned(w.curve) > 3 ||
        unsigned(w.firingMode) > 2 || w.burstCount < 1 || w.burstCount > 100 ||
        w.triggerThreshold < 1 || w.triggerThreshold > 1000 || w.pressMs > 60000 ||
        w.releaseMs > 60000 || !raw(w.horizontal) || !raw(w.vertical) ||
        !raw(w.adsHorizontal) || !raw(w.adsVertical) ||
        !raw(w.firstHorizontal) || !raw(w.firstVertical)) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!w.stageMs[i] || w.stageMs[i] > 60000 ||
            (i && w.stageMs[i] <= w.stageMs[i-1]) || !raw(w.stageX[i]) || !raw(w.stageY[i])) return false;
    }
    return true;
}
} // namespace oag
