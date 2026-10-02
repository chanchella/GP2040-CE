#include "oag/mapping/smart_combo_engine.h"
#include <algorithm>
#include <limits>
namespace oag {
namespace {
bool tap(OagSmartTrigger t) { return t <= OagSmartTrigger::Multi; }
bool level(OagSmartTrigger t) { return t == OagSmartTrigger::Held || t == OagSmartTrigger::Stick || t == OagSmartTrigger::Threshold; }
unsigned count(const OagSmartCondition& c) {
    return c.trigger == OagSmartTrigger::Double ? 2 : c.trigger == OagSmartTrigger::Triple ? 3 : c.trigger == OagSmartTrigger::Multi ? c.taps : 1;
}
std::size_t horizonIndex(const OagSmartTarget& t) {
    switch (t.kind) {
    case OagSmartTargetKind::Pad: if (t.code >= 1 && t.code <= 34) return t.code - 1; break;
    case OagSmartTargetKind::Key: if (t.code >= 1 && t.code <= 255) return 34 + t.code - 1; break;
    case OagSmartTargetKind::Mouse:
        if (t.code && !(t.code & (t.code - 1))) {
            unsigned bit = 0; for (auto v = t.code; v > 1; v >>= 1) ++bit;
            return 289 + bit;
        }
        break;
    case OagSmartTargetKind::Wheel: if (t.code == 1 || t.code == 2) return 305 + t.code - 1; break;
    }
    return 307;
}
std::uint64_t ms(std::uint16_t v) { return std::uint64_t(v) * 1000; }
unsigned specificity(const OagSmartBranch& b) {
    unsigned score = b.priority * 4096u + b.conditionCount * 256u;
    for (std::size_t c = 0; c < b.conditionCount; ++c) {
        const auto& cond = b.conditions[c];
        score += cond.targetCount * 16u;
        if (cond.trigger == OagSmartTrigger::Sequence) score += 128;
        else if (cond.trigger == OagSmartTrigger::Chord) score += 96;
        else if (cond.trigger == OagSmartTrigger::Long || cond.trigger == OagSmartTrigger::Hold) score += 64;
        else if (tap(cond.trigger)) score += count(cond) * 8;
    }
    return score;
}
}
void OagSmartComboEngine::reset() {
    // Never materialize the whole 30 KiB detector bank on the Pico stack.
    for (auto& p : detectors_) for (auto& b : p) for (auto& d : b) d = Detector {};
    for (auto& b : previousBranches_) b.fill(false);
    for (auto& r : runners_) r = Runner {};
    output_ = {}; filtered_ = {}; game_ = nullptr; compiled_ = false;
}
void OagSmartComboEngine::compileHorizons(const OagSmartGame& g) {
    horizons_.fill(Horizon {});
    // Configuration is immutable until reset (the firmware uses its RAM
    // revision). Recognition does no quadratic scan on a button edge.
    for (const auto& p : g.programs) if (p.enabled)
        for (std::size_t b = 0; b < p.branchCount; ++b) if (p.branches[b].enabled && !p.branches[b].otherwise)
            for (std::size_t c = 0; c < p.branches[b].conditionCount; ++c) {
                const auto& cond = p.branches[b].conditions[c]; if (cond.negate) continue;
                for (std::size_t t = 0; t < cond.targetCount; ++t) {
                    const auto index = horizonIndex(cond.targets[t]); if (index >= horizons_.size()) continue;
                    auto& h = horizons_[index];
                    if (tap(cond.trigger)) h.windowMs = std::max(h.windowMs, cond.windowMs);
                    if (cond.trigger == OagSmartTrigger::Sequence) h.windowMs = std::max(h.windowMs, cond.sequenceMs);
                    if (cond.trigger == OagSmartTrigger::Chord) h.windowMs = std::max(h.windowMs, cond.chordMs);
                    if (cond.trigger == OagSmartTrigger::Long || cond.trigger == OagSmartTrigger::Hold) h.longMs = std::min(h.longMs, cond.holdMs);
                }
            }
    compiled_ = true;
}
void OagSmartComboEngine::consumeNativeWheel() {
    output_.mouse.wheel = 0;
    for (auto& r : runners_) r.held.mouse.wheel = r.transient.mouse.wheel = 0;
}
bool OagSmartComboEngine::active() const { for (const auto& r : runners_) if (r.active) return true; return false; }
bool OagSmartComboEngine::active(std::size_t slot) const { return slot < runners_.size() && runners_[slot].active; }
bool OagSmartComboEngine::needsTick() const {
    if (active()) return true;
    for (const auto& p : detectors_) for (const auto& b : p) for (const auto& d : b)
        if (d.session || d.sequence || d.previous) return true;
    return false;
}
void OagSmartComboEngine::stop(std::size_t slot) { if (slot < runners_.size()) runners_[slot] = {}; }
void OagSmartComboEngine::start(std::size_t s, std::size_t b, std::uint64_t now, bool testMode) {
    auto& r = runners_[s]; r = {}; r.active = true; r.branch = static_cast<std::uint8_t>(b); r.deadlineUs = now; r.test = testMode;
    ++starts_; lastSlot_ = static_cast<int>(s); lastBranch_ = static_cast<int>(b);
}
bool OagSmartComboEngine::test(const OagSmartGame& g, std::size_t s, std::size_t b, std::uint64_t now) {
    if (s >= g.programs.size() || b >= g.programs[s].branchCount || !g.programs[s].enabled || !g.programs[s].branches[b].enabled) return false;
    const char* error = nullptr; if (!oagSmartValidate(g.programs[s], error)) return false;
    if (game_ != &g) compiled_ = false;
    game_ = &g; start(s, b, now, true); return true;
}
OagSmartComboEngine::Result OagSmartComboEngine::detect(const OagSmartCondition& c, Detector& d,
    const OagSmartGame&, const OagSmartInput& input, std::uint64_t now) {
    std::uint8_t down = 0;
    for (std::size_t t = 0; t < c.targetCount; ++t) if (oagSmartDown(c.targets[t], input, c.thresholdPermille)) down |= 1u << t;
    auto rising = static_cast<std::uint8_t>(down & ~d.previous);
    // Wheel reports are events, not held levels. Count a changed input report
    // only; firmware clears wheel motion after composition.
    if (c.targets[0].kind == OagSmartTargetKind::Wheel && down) {
        rising = (!d.wheelSeen || input.mouse.generation != d.wheelGeneration) ? down : 0;
        d.wheelSeen = true; d.wheelGeneration = input.mouse.generation;
    }
    const auto falling = static_cast<std::uint8_t>(d.previous & ~down);
    d.previous = down;
    for (std::size_t t = 0; t < c.targetCount; ++t) if (rising & (1u << t)) d.pressedUs[t] = now;
    Result out;
    if (d.blocked) {
        if (!down) d.blocked = false;
        if (level(c.trigger)) out.value = down != 0;
        return out;
    }
    if (level(c.trigger)) { out.value = down != 0; out.event = rising != 0; return out; }
    if (c.trigger == OagSmartTrigger::Release) { out.value = out.event = out.resolved = falling != 0; return out; }
    if (c.trigger == OagSmartTrigger::Long || c.trigger == OagSmartTrigger::Hold) {
        if (rising) { d.downUs = now; d.longFired = false; }
        if (down && !d.longFired && now - d.downUs >= ms(c.holdMs)) {
            out.value = out.event = out.resolved = true; d.longFired = true;
        }
        if (falling && !d.longFired) out.resolved = true;
        if (!down) d.longFired = false;
        return out;
    }
    if (c.trigger == OagSmartTrigger::Chord) {
        const auto full = static_cast<std::uint8_t>((1u << c.targetCount) - 1u);
        if (down == full && rising) {
            auto lo = now, hi = std::uint64_t(0);
            for (std::size_t t = 0; t < c.targetCount; ++t) { lo = std::min(lo, d.pressedUs[t]); hi = std::max(hi, d.pressedUs[t]); }
            if (hi - lo <= ms(c.chordMs)) out.value = out.event = out.resolved = true;
        }
        return out;
    }
    if (c.trigger == OagSmartTrigger::Sequence) {
        if (d.sequence && now - d.firstUs > ms(c.sequenceMs)) { d.sequence = 0; out.resolved = true; }
        if (rising) {
            const auto expected = d.sequence;
            if (rising & (1u << expected)) {
                if (!d.sequence) d.firstUs = now;
                ++d.sequence;
                if (d.sequence == c.targetCount) { out.value = out.event = out.resolved = true; d.sequence = 0; }
            } else {
                d.sequence = (rising & 1u) ? 1 : 0; if (d.sequence) d.firstUs = now;
            }
        }
        return out;
    }
    if (!tap(c.trigger)) return out;
    const bool wheel = c.targets[0].kind == OagSmartTargetKind::Wheel;
    if (rising) {
        d.downUs = now;
        d.lastTapUs = now;
        if (!d.session) {
            d.session = true; d.firstUs = now; d.taps = 0; d.resolveMs = c.windowMs; d.longLimitMs = 60000;
            const auto index = horizonIndex(c.targets[0]);
            if (index < horizons_.size()) { d.resolveMs = std::max(d.resolveMs, horizons_[index].windowMs); d.longLimitMs = horizons_[index].longMs; }
        }
    }
    if ((wheel ? rising != 0 : falling != 0) && d.session) {
        if (!wheel && now - d.downUs >= ms(d.longLimitMs)) { d.session = false; d.taps = 0; out.resolved = true; return out; }
        if (d.taps < 255) ++d.taps;
    }
    if (d.session && (!down || wheel) && now - d.firstUs >= ms(d.resolveMs)) {
        out.resolved = true;
        const auto last = d.lastTapUs;
        out.value = out.event = d.taps == count(c) && last - d.firstUs <= ms(c.windowMs);
        d.session = false; d.taps = 0;
    }
    return out;
}
bool OagSmartComboEngine::anchorHeld(const OagSmartBranch& branch, const OagSmartInput& input) const {
    // Temporal recognizers have already fired. Lifetime follows the physical
    // WHEN input, never the generated output or the momentary event boolean.
    bool group = true, expression = false;
    for (std::size_t c = 0; c < branch.conditionCount; ++c) {
        const auto& cond = branch.conditions[c];
        bool all = true;
        if (cond.trigger == OagSmartTrigger::Sequence) all = oagSmartDown(cond.targets[cond.targetCount - 1], input, cond.thresholdPermille);
        else for (std::size_t t = 0; t < cond.targetCount; ++t) all &= oagSmartDown(cond.targets[t], input, cond.thresholdPermille);
        const bool value = cond.negate ? !all : all;
        if (c && cond.join == OagSmartJoin::Or) { expression |= group; group = value; }
        else group &= value;
    }
    return branch.conditionCount != 0 && (expression || group);
}
bool OagSmartComboEngine::overlaps(const OagSmartBranch& a, const OagSmartBranch& b) const {
    const auto hasEvent = [](const OagSmartBranch& branch) {
        for (std::size_t c = 0; c < branch.conditionCount; ++c)
            if (!branch.conditions[c].negate && !level(branch.conditions[c].trigger)) return true;
        return false;
    };
    const bool eventA = hasEvent(a), eventB = hasEvent(b);
    for (std::size_t x = 0; x < a.conditionCount; ++x) if (!a.conditions[x].negate)
        for (std::size_t y = 0; y < b.conditionCount; ++y) if (!b.conditions[y].negate)
            // A shared held modifier alone is not a shared input gesture.
            if (!(eventA && eventB && (level(a.conditions[x].trigger) || level(b.conditions[y].trigger))))
            for (std::size_t t = 0; t < a.conditions[x].targetCount; ++t)
                for (std::size_t u = 0; u < b.conditions[y].targetCount; ++u)
                    if (oagSmartSame(a.conditions[x].targets[t], b.conditions[y].targets[u])) return true;
    return false;
}
void OagSmartComboEngine::claim(const OagSmartBranch& branch) {
    if (!game_) return;
    for (std::size_t p = 0; p < game_->programs.size(); ++p) for (std::size_t b = 0; b < game_->programs[p].branchCount; ++b) {
        const auto& other = game_->programs[p].branches[b];
        for (std::size_t c = 0; c < other.conditionCount; ++c) {
            bool shared = false;
            for (std::size_t x = 0; x < branch.conditionCount; ++x) if (!branch.conditions[x].negate && !level(branch.conditions[x].trigger))
                for (std::size_t t = 0; t < branch.conditions[x].targetCount; ++t)
                    for (std::size_t u = 0; u < other.conditions[c].targetCount; ++u)
                        shared |= oagSmartSame(branch.conditions[x].targets[t], other.conditions[c].targets[u]);
            if (shared) { auto& d = detectors_[p][b][c]; d.session = false; d.taps = d.sequence = 0; d.blocked = d.previous != 0; }
        }
    }
}
void OagSmartComboEngine::execute(const OagSmartBranch& b, Runner& r, const OagSmartInput& input, std::uint64_t now) {
    r.held.mouse.wheel = r.transient.mouse.wheel = 0;
    if (r.finished) return;
    // A bounded interpreter cannot monopolize the USB/BT scheduler, even if
    // an edited loop contains only zero-duration actions.
    for (unsigned budget = 0; budget < 32 && r.active; ++budget) {
        if (now < r.deadlineUs) return;
        if (r.step >= b.actionCount) {
            if (!r.test && (b.mode == OagSmartMode::RepeatHeld || b.mode == OagSmartMode::LoopUntilAgain)) {
                r.step = 0; r.entered = false; r.loops = {}; r.transient = {}; r.deadlineUs = now + 1000; return;
            }
            r.finished = true; r.transient = {};
            if (r.test || (b.mode == OagSmartMode::Once || b.mode == OagSmartMode::StopOnRelease)) {
                if (!r.keepAgain && !r.keepRelease) { r = {}; return; }
            }
            return;
        }
        const auto& a = b.actions[r.step];
        if (!r.entered) { r.entered = true; r.stage = 0; r.pulses = 0; r.deadlineUs = now + ms(a.beforeMs); }
        if (now < r.deadlineUs) return;
        auto targets = [&](bool down, OagSmartOutput& out) { for (std::size_t t = 0; t < a.targetCount; ++t) oagSmartSet(a.targets[t], down, out); };
        auto finish = [&]() { r.stage = 3; r.deadlineUs = now + ms(a.afterMs); };
        if (r.stage == 0) {
            r.stage = 1; r.deadlineUs = now + ms(a.durationMs);
            switch (a.kind) {
            case OagSmartActionKind::Press: targets(true, r.held); break;
            case OagSmartActionKind::Release: targets(false, r.held); r.deadlineUs = now + ms(a.releaseMs); break;
            case OagSmartActionKind::Hold:
                if (a.durationMs == 0) {
                    targets(true, r.held); r.keepAgain |= a.lifetime == 1; r.keepRelease |= a.lifetime == 2; finish();
                } else targets(true, r.transient);
                break;
            case OagSmartActionKind::Tap: case OagSmartActionKind::Pulse: case OagSmartActionKind::MultiPress:
            case OagSmartActionKind::StickDirection: targets(true, r.transient); break;
            case OagSmartActionKind::StickValue: {
                const bool left = a.targets[0].code >= 26;
                auto& x = left ? r.transient.pad.lx : r.transient.pad.rx; auto& y = left ? r.transient.pad.ly : r.transient.pad.ry;
                constexpr auto max = std::numeric_limits<std::int32_t>::max();
                x = static_cast<std::int32_t>(std::int64_t(a.valueX) * max / 1000); y = static_cast<std::int32_t>(std::int64_t(a.valueY) * max / 1000);
                r.transient.axes |= left ? 1u : 2u; r.transient.pad.connected = true; break;
            }
            case OagSmartActionKind::TriggerValue: {
                const bool left = a.targets[0].code == 7; auto& v = left ? r.transient.pad.leftTrigger : r.transient.pad.rightTrigger;
                v = 65535u * static_cast<unsigned>(a.valueX) / 1000u; r.transient.axes |= left ? 4u : 8u; r.transient.pad.connected = true; break;
            }
            case OagSmartActionKind::ReleaseAll: r.held = {}; r.transient = {}; r.keepAgain = r.keepRelease = false; finish(); break;
            case OagSmartActionKind::Stop: r = {}; return;
            case OagSmartActionKind::Repeat: case OagSmartActionKind::Loop:
                if (!a.repeatCount || ++r.loops[r.step] < a.repeatCount) {
                    r.step = a.loopFrom; r.entered = false; r.deadlineUs = now + ms(a.intervalMs); return;
                }
                r.loops[r.step] = 0; finish(); break;
            case OagSmartActionKind::Wait: break;
            case OagSmartActionKind::WaitUntilPressed: case OagSmartActionKind::WaitUntilReleased: r.deadlineUs = now; break;
            }
            if (now < r.deadlineUs) return;
        }
        if (r.stage == 1) {
            if (a.kind == OagSmartActionKind::WaitUntilPressed || a.kind == OagSmartActionKind::WaitUntilReleased) {
                bool satisfied = true;
                for (std::size_t t = 0; t < a.targetCount; ++t)
                    satisfied &= oagSmartDown(a.targets[t], input) == (a.kind == OagSmartActionKind::WaitUntilPressed);
                if (!satisfied) return;
                finish();
            } else if (a.kind == OagSmartActionKind::Tap || a.kind == OagSmartActionKind::Pulse || a.kind == OagSmartActionKind::MultiPress) {
                r.transient = {}; ++r.pulses; r.stage = 2; r.deadlineUs = now + ms(a.releaseMs) + ms(a.intervalMs);
            } else if (a.kind == OagSmartActionKind::Hold || a.kind == OagSmartActionKind::StickDirection ||
                a.kind == OagSmartActionKind::StickValue || a.kind == OagSmartActionKind::TriggerValue) {
                r.transient = {}; r.stage = 2; r.deadlineUs = now + ms(a.releaseMs);
            } else finish();
            if (now < r.deadlineUs) return;
        }
        if (r.stage == 2) {
            const bool pulse = a.kind == OagSmartActionKind::Tap || a.kind == OagSmartActionKind::Pulse || a.kind == OagSmartActionKind::MultiPress;
            if (pulse && (!a.repeatCount || r.pulses < a.repeatCount)) {
                targets(true, r.transient); r.stage = 1; r.deadlineUs = now + ms(a.durationMs); return;
            }
            finish(); if (now < r.deadlineUs) return;
        }
        if (r.stage == 3) { ++r.step; r.entered = false; }
    }
}
void OagSmartComboEngine::tick(const OagSmartGame& g, const OagSmartInput& input, std::uint64_t now) {
    if (!compiled_ || game_ != &g) compileHorizons(g);
    game_ = &g; filtered_ = input; output_ = {};
    struct Candidate { std::uint8_t slot = 0, branch = 0, anchor = 0; unsigned score = 0; };
    std::array<Candidate, kOagSmartCombos * kOagSmartBranches> candidates {}; std::size_t size = 0;
    for (std::size_t s = 0; s < g.programs.size(); ++s) {
        const auto& p = g.programs[s]; if (!p.enabled) { stop(s); continue; }
        bool resolved = false, matched = false;
        std::uint8_t resolvedBranch = 0;
        for (std::size_t b = 0; b < p.branchCount; ++b) {
            const auto& branch = p.branches[b]; if (!branch.enabled || branch.otherwise) continue;
            bool group = true, expression = false, event = false, groupEvent = false;
            for (std::size_t c = 0; c < branch.conditionCount; ++c) {
                auto result = detect(branch.conditions[c], detectors_[s][b][c], g, input, now);
                resolved |= result.resolved;
                if (result.resolved) resolvedBranch = static_cast<std::uint8_t>(b);
                const bool value = branch.conditions[c].negate ? !result.value : result.value;
                if (c && branch.conditions[c].join == OagSmartJoin::Or) {
                    expression |= group; event |= group && groupEvent;
                    group = value; groupEvent = result.event && !branch.conditions[c].negate;
                }
                else { group &= value; groupEvent |= result.event && !branch.conditions[c].negate; }
            }
            expression |= group;
            event |= group && groupEvent;
            const bool rising = expression && !previousBranches_[s][b]; previousBranches_[s][b] = expression;
            if (expression && (event || rising)) { candidates[size++] = {static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(b), specificity(branch)}; matched = true; }
        }
        bool pending = false;
        for (std::size_t b = 0; b < p.branchCount; ++b) for (std::size_t c = 0; c < p.branches[b].conditionCount; ++c)
            pending |= detectors_[s][b][c].session || detectors_[s][b][c].sequence;
        if (resolved && !matched && !pending) for (std::size_t b = 0; b < p.branchCount; ++b)
            if (p.branches[b].enabled && p.branches[b].otherwise) candidates[size++] = {static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(b), resolvedBranch, 0};
    }
    // Deterministic priority: user priority, specificity, tap count, then slot
    // and branch order. Losers sharing the same gesture are consumed.
    std::sort(candidates.begin(), candidates.begin() + size, [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.slot != b.slot ? a.slot < b.slot : a.branch < b.branch;
    });
    std::array<const OagSmartBranch*, kOagSmartCombos> accepted {}; std::size_t acceptedCount = 0;
    std::array<bool, kOagSmartCombos> acceptedSlots {};
    for (std::size_t i = 0; i < size; ++i) {
        const auto& candidate = candidates[i]; const auto& p = g.programs[candidate.slot]; const auto& b = p.branches[candidate.branch];
        if (acceptedSlots[candidate.slot]) continue;
        const auto& claims = p.branches[candidate.anchor];
        bool conflict = false;
        for (std::size_t j = 0; j < acceptedCount; ++j) conflict |= overlaps(claims, *accepted[j]);
        if (conflict) continue;
        auto& r = runners_[candidate.slot];
        if (r.active && r.branch == candidate.branch && (b.mode == OagSmartMode::Toggle || b.mode == OagSmartMode::LoopUntilAgain || r.keepAgain)) { stop(candidate.slot); }
        else if (!r.active || g.programs[candidate.slot].branches[r.branch].cancelable) start(candidate.slot, candidate.branch, now);
        else continue;
        claim(claims); accepted[acceptedCount++] = &claims;
        acceptedSlots[candidate.slot] = true;
    }
    for (std::size_t s = 0; s < runners_.size(); ++s) {
        auto& r = runners_[s]; if (!r.active) continue;
        const auto& p = g.programs[s]; const auto& b = p.branches[r.branch];
        const bool held = anchorHeld(b, input);
        if (!r.test && (b.mode == OagSmartMode::WhileHeld || b.mode == OagSmartMode::RepeatHeld || b.mode == OagSmartMode::StopOnRelease || r.keepRelease) && !held) { stop(s); continue; }
        execute(b, r, input, now);
        if (!r.active) continue;
        oagSmartMerge(r.held, output_); oagSmartMerge(r.transient, output_);
    }
    // Consume configured trigger inputs throughout recognition, including the
    // waiting window. Raw inputs still feed detection; only the composed copy
    // is filtered. This also isolates old combos from owned smart gestures.
    for (std::size_t s = 0; s < g.programs.size(); ++s) if (g.programs[s].enabled && g.programs[s].consumeInput)
        for (std::size_t b = 0; b < g.programs[s].branchCount; ++b) if (g.programs[s].branches[b].enabled)
            for (std::size_t c = 0; c < g.programs[s].branches[b].conditionCount; ++c) if (!g.programs[s].branches[b].conditions[c].negate)
                for (std::size_t t = 0; t < g.programs[s].branches[b].conditions[c].targetCount; ++t)
                    oagSmartConsume(g.programs[s].branches[b].conditions[c].targets[t], filtered_);
}
} // namespace oag
