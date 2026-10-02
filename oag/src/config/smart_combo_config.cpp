#include "oag/config/smart_combo_config.h"
#include <cstring>
namespace oag {
namespace {
constexpr auto crcTable() {
    std::array<std::uint32_t, 256> table {};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto v = i; for (int b = 0; b < 8; ++b) v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1u)));
        table[i] = v;
    }
    return table;
}
constexpr auto crcLookup = crcTable(); // Flash only; fast game-switch validation
}
bool oagSmartTargetValid(const OagSmartTarget& t) {
    if (t.strength > 100) return false;
    switch (t.kind) {
    case OagSmartTargetKind::Pad: return t.code >= 1 && t.code <= 34;
    case OagSmartTargetKind::Key: return t.code >= 1 && t.code <= 255;
    case OagSmartTargetKind::Mouse: return t.code && !(t.code & (t.code - 1));
    case OagSmartTargetKind::Wheel: return t.code == 1 || t.code == 2;
    }
    return false;
}
bool oagSmartValidate(const OagSmartProgram& p, const char*& error) {
    error = nullptr;
    auto fail = [&](const char* e) { error = e; return false; };
    if (p.enabled > 1 || p.consumeInput > 1 || p.branchCount < 1 || p.branchCount > kOagSmartBranches)
        return fail("Invalid OAG program flags/branches");
    if (!std::memchr(p.name.data(), 0, p.name.size())) return fail("OAG name exceeds 32 bytes");
    // Disabled slots may contain blank targets, but their wire counts must
    // remain safe for every reader (including the no-code editor).
    for (std::size_t b = 0; b < p.branchCount; ++b) {
        const auto& v = p.branches[b];
        if (v.enabled > 1 || v.otherwise > 1 || v.cancelable > 1 || static_cast<unsigned>(v.mode) > 5 ||
            v.conditionCount > kOagSmartConditions || v.actionCount > kOagSmartActions) return fail("Invalid OAG branch structure");
        for (std::size_t c = 0; c < v.conditionCount; ++c) {
            const auto& t = v.conditions[c];
            if (t.targetCount > kOagSmartSequence || static_cast<unsigned>(t.trigger) > 11 ||
                static_cast<unsigned>(t.join) > 1 || t.negate > 1) return fail("Invalid OAG condition structure");
        }
        for (std::size_t a = 0; a < v.actionCount; ++a) {
            const auto& t = v.actions[a];
            if (t.targetCount > kOagSmartTargets || static_cast<unsigned>(t.kind) > 15 || t.lifetime > 2) return fail("Invalid OAG action structure");
        }
    }
    if (!p.enabled) return true;
    bool hasIf = false;
    for (std::size_t b = 0; b < p.branchCount; ++b) {
        const auto& branch = p.branches[b];
        if (branch.enabled > 1 || branch.otherwise > 1 || branch.cancelable > 1 || static_cast<unsigned>(branch.mode) > 5)
            return fail("Invalid OAG branch flags/mode");
        if (!branch.enabled) continue;
        if (branch.actionCount < 1 || branch.actionCount > kOagSmartActions || branch.conditionCount > kOagSmartConditions)
            return fail("OAG action/condition capacity exceeded");
        if (branch.otherwise) {
            if (!hasIf || b + 1 != p.branchCount || branch.conditionCount) return fail("ELSE must be last, after WHEN, with no conditions");
        } else {
            hasIf = true;
            if (!branch.conditionCount) return fail("WHEN needs a condition");
            bool positive = false;
            bool groupPositive = false;
            for (std::size_t c = 0; c < branch.conditionCount; ++c) {
                const auto& cond = branch.conditions[c];
                if (c && cond.join == OagSmartJoin::Or) {
                    if (!groupPositive) return fail("Every OR group needs a positive OAG input alongside NOT");
                    groupPositive = false;
                }
                groupPositive |= !cond.negate;
                if (static_cast<unsigned>(cond.trigger) > 11 || static_cast<unsigned>(cond.join) > 1 || cond.negate > 1)
                    return fail("Invalid OAG condition logic");
                if (!cond.targetCount || cond.targetCount > kOagSmartSequence || !cond.windowMs || !cond.holdMs ||
                    !cond.sequenceMs || !cond.chordMs || !cond.thresholdPermille || cond.thresholdPermille > 1000 ||
                    cond.windowMs > 60000 || cond.holdMs > 60000 || cond.sequenceMs > 60000 || cond.chordMs > 60000)
                    return fail("OAG timing must be 1..60000 ms; threshold 1..1000");
                if (cond.trigger != OagSmartTrigger::Chord && cond.trigger != OagSmartTrigger::Sequence && cond.targetCount != 1)
                    return fail("Use Chord/Sequence for multiple input buttons");
                if (cond.trigger == OagSmartTrigger::Chord && cond.targetCount < 2) return fail("Chord needs two buttons");
                if (cond.trigger == OagSmartTrigger::Sequence && cond.targetCount < 2) return fail("Sequence needs two entries");
                if (cond.trigger == OagSmartTrigger::Multi && (cond.taps < 1 || cond.taps > 16)) return fail("Multi Tap count is 1..16");
                positive |= !cond.negate;
                for (std::size_t t = 0; t < cond.targetCount; ++t) {
                    if (!oagSmartTargetValid(cond.targets[t])) return fail("Invalid OAG input button");
                    if (cond.targets[t].kind == OagSmartTargetKind::Wheel &&
                        (cond.trigger == OagSmartTrigger::Held || cond.trigger == OagSmartTrigger::Long || cond.trigger == OagSmartTrigger::Hold || cond.trigger == OagSmartTrigger::Release ||
                         cond.trigger == OagSmartTrigger::Chord || cond.trigger == OagSmartTrigger::Sequence))
                        return fail("Mouse wheel is an event, not a held button");
                    if (cond.trigger == OagSmartTrigger::Chord) for (std::size_t q = 0; q < t; ++q)
                        if (cond.targets[q].kind == cond.targets[t].kind && cond.targets[q].code == cond.targets[t].code)
                            return fail("Chord buttons must be different");
                }
                if (cond.trigger == OagSmartTrigger::Threshold &&
                    (cond.targets[0].kind != OagSmartTargetKind::Pad || (cond.targets[0].code != 7 && cond.targets[0].code != 8)))
                    return fail("Threshold uses L2/R2");
                if (cond.trigger == OagSmartTrigger::Stick &&
                    (cond.targets[0].kind != OagSmartTargetKind::Pad || cond.targets[0].code < 18 || cond.targets[0].code > 33))
                    return fail("Stick trigger needs one of the 16 stick directions");
            }
            if (!positive) return fail("WHEN needs a positive input condition alongside NOT");
            if (!groupPositive) return fail("Every OR group needs a positive OAG input alongside NOT");
        }
        for (std::size_t a = 0; a < branch.actionCount; ++a) {
            const auto& action = branch.actions[a];
            if (static_cast<unsigned>(action.kind) > 15 || action.targetCount > kOagSmartTargets || action.lifetime > 2 ||
                action.durationMs > 60000 || action.releaseMs > 60000 || action.beforeMs > 60000 || action.afterMs > 60000 || action.intervalMs > 60000)
                return fail("Invalid OAG action/timing");
            const auto kind = action.kind;
            const bool noTarget = kind == OagSmartActionKind::Wait || kind == OagSmartActionKind::Repeat || kind == OagSmartActionKind::Loop ||
                kind == OagSmartActionKind::Stop || kind == OagSmartActionKind::ReleaseAll;
            if (!noTarget && !action.targetCount) return fail("OAG action needs a button");
            for (std::size_t t = 0; t < action.targetCount; ++t) if (!oagSmartTargetValid(action.targets[t])) return fail("Invalid OAG output button");
            if ((kind == OagSmartActionKind::Repeat || kind == OagSmartActionKind::Loop) && action.loopFrom >= a)
                return fail("Repeat/Loop must point to an earlier action");
            if ((kind == OagSmartActionKind::Tap || kind == OagSmartActionKind::MultiPress) && !action.repeatCount)
                return fail("Use PULSE or LOOP for infinite repetition");
            if (kind == OagSmartActionKind::StickValue || kind == OagSmartActionKind::StickDirection) {
                if (action.targetCount != 1 || action.targets[0].kind != OagSmartTargetKind::Pad || action.targets[0].code < 18 || action.targets[0].code > 33)
                    return fail("Stick action needs a stick direction");
                if (action.valueX < -1000 || action.valueX > 1000 || action.valueY < -1000 || action.valueY > 1000)
                    return fail("OAG stick values are -1000..1000");
            }
            if (kind == OagSmartActionKind::TriggerValue && (action.targetCount != 1 || action.targets[0].kind != OagSmartTargetKind::Pad ||
                (action.targets[0].code != 7 && action.targets[0].code != 8) || action.valueX < 0 || action.valueX > 1000))
                return fail("Trigger Value uses L2/R2 and 0..1000");
        }
    }
    return hasIf || fail("Enable at least one WHEN branch");
}
std::uint32_t oagSmartCrc(const void* bytes, std::size_t length) {
    auto p = static_cast<const std::uint8_t*>(bytes); std::uint32_t crc = 0xffffffffu;
    while (length--) crc = crcLookup[(crc ^ *p++) & 255u] ^ (crc >> 8);
    return ~crc;
}
bool oagSmartRecordValid(const OagSmartRecord& r, std::size_t game) {
    if (r.magic != OagSmartRecord::kMagic || r.version != OagSmartRecord::kVersion || r.game != game || r.crc != oagSmartCrc(&r.payload, sizeof(r.payload))) return false;
    for (const auto& p : r.payload.programs) { const char* error = nullptr; if (!oagSmartValidate(p, error)) return false; }
    return true;
}
} // namespace oag
