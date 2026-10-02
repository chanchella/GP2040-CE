#include "oag/mapping/smart_combo_legacy.h"
namespace oag {
namespace {
bool matches(const OagSmartTarget& t, const DiamondComboTrigger& old) {
    switch (old.kind) {
    case DiamondComboTriggerKind::LogicalControl: return t.kind == OagSmartTargetKind::Pad && t.code == old.code;
    case DiamondComboTriggerKind::KeyboardUsage:
        if (t.kind != OagSmartTargetKind::Key) return false;
        return t.code == old.code || (t.code >= 224 && t.code <= 231 && (old.modifiers & (1u << (t.code - 224))));
    case DiamondComboTriggerKind::MouseButton: return t.kind == OagSmartTargetKind::Mouse && (t.code & old.code);
    case DiamondComboTriggerKind::MouseWheel: return t.kind == OagSmartTargetKind::Wheel && (t.code == 1 ? old.code == 1 : old.code != 1);
    }
    return false;
}
}
std::uint16_t oagSmartLegacyMask(const OagSmartGame& smart, const std::array<DiamondComboProgram, kDiamondComboSlots>& old) {
    std::uint16_t mask = 0;
    for (std::size_t slot = 0; slot < old.size(); ++slot) if (old[slot].enabled)
        for (const auto& p : smart.programs) if (p.enabled)
            for (std::size_t b = 0; b < p.branchCount; ++b) if (p.branches[b].enabled && !p.branches[b].otherwise)
                for (std::size_t c = 0; c < p.branches[b].conditionCount; ++c) if (!p.branches[b].conditions[c].negate)
                    for (std::size_t t = 0; t < p.branches[b].conditions[c].targetCount; ++t)
                        for (const auto& trigger : old[slot].triggers) if (trigger.enabled && matches(p.branches[b].conditions[c].targets[t], trigger)) mask |= 1u << slot;
    return mask;
}
} // namespace oag
