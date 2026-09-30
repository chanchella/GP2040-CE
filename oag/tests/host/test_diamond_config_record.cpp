#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "oag/config/diamond_config_record.h"

int main() {
    using namespace oag;

    DiamondConfigRecord a {};
    a.generation = 7;
    a.payload.runtime.activeGame = 2;
    std::strncpy(
        a.payload.security.adminUsername.data(),
        "admin-a",
        a.payload.security.adminUsername.size() - 1
    );
    finalizeDiamondConfigRecord(a);

    assert(validateDiamondConfigRecord(a));
    assert(selectNewestDiamondConfigRecord(&a, nullptr) == &a);

    DiamondConfigRecord b = a;
    b.generation = 8;
    b.payload.runtime.activeGame = 3;
    finalizeDiamondConfigRecord(b);

    assert(validateDiamondConfigRecord(b));
    assert(selectNewestDiamondConfigRecord(&a, &b) == &b);

    // A torn/corrupted newest slot must fall back to the previous valid slot.
    b.payload.runtime.activeGame ^= 1u;
    assert(!validateDiamondConfigRecord(b));
    assert(selectNewestDiamondConfigRecord(&a, &b) == &a);

    // Header corruption must also fail closed.
    DiamondConfigRecord badHeader = a;
    badHeader.magic ^= 0x01u;
    assert(!validateDiamondConfigRecord(badHeader));

    // Generation comparison is wrap-safe for adjacent generations.
    DiamondConfigRecord old {};
    old.generation = 0xFFFFFFFFu;
    finalizeDiamondConfigRecord(old);
    DiamondConfigRecord wrapped {};
    wrapped.generation = 0u;
    finalizeDiamondConfigRecord(wrapped);
    assert(selectNewestDiamondConfigRecord(&old, &wrapped) == &wrapped);

    // V5 spans multiple flash sectors to persist OAG names and multi-input programmable combos.
    static_assert(sizeof(DiamondConfigRecord) <= 16384);

    // Named content is covered by the same CRC as runtime settings.
    DiamondConfigRecord named = a;
    std::strncpy(
        named.payload.names.games[0].data(),
        "Blood Strike",
        named.payload.names.games[0].size() - 1
    );
    finalizeDiamondConfigRecord(named);
    assert(validateDiamondConfigRecord(named));
    named.payload.names.games[0][0] ^= 1;
    assert(!validateDiamondConfigRecord(named));

    // OAG weapon recoil settings are protected by the same CRC.
    DiamondConfigRecord recoil = a;
    recoil.payload.runtime.games[1].weapons[2].enabled = true;
    recoil.payload.runtime.games[1].weapons[2].horizontalHalfPermille = -24;
    recoil.payload.runtime.games[1].weapons[2].verticalHalfPermille = 36;
    recoil.payload.runtime.games[1].weapons[2].tickMs = 33;
    finalizeDiamondConfigRecord(recoil);
    assert(validateDiamondConfigRecord(recoil));
    recoil.payload.runtime.games[1].weapons[2].verticalHalfPermille ^= 1;
    assert(!validateDiamondConfigRecord(recoil));

    // Millisecond OAG combo timing is persistent and CRC-protected.
    DiamondConfigRecord combo = a;
    combo.payload.names.comboTiming[3].enabled = true;
    combo.payload.names.comboTiming[3].pressMs = 125;
    combo.payload.names.comboTiming[3].delayAfterMs = 37;
    combo.payload.names.comboTiming[3].repeatCount = 4;
    finalizeDiamondConfigRecord(combo);
    assert(validateDiamondConfigRecord(combo));
    combo.payload.names.comboTiming[3].pressMs ^= 1u;
    assert(!validateDiamondConfigRecord(combo));

    // Programmable combo triggers, cancellation and millisecond steps are
    // covered by the same CRC-protected V4 record.
    DiamondConfigRecord program = a;
    auto& p = program.payload.names.comboPrograms[2];
    p.enabled = true;
    p.activation = DiamondComboActivationMode::WhileHeld;
    p.repeat = DiamondComboRepeatMode::AutoRepeat;
    p.passTriggerThrough = false;
    p.cancelOnTriggerRelease = true;
    p.triggers[0].enabled = true;
    p.triggers[0].kind = DiamondComboTriggerKind::LogicalControl;
    p.triggers[0].code =
        static_cast<std::uint16_t>(DiamondLogicalControl::West);
    p.stepCount = 2;
    p.steps[0].enabled = true;
    p.steps[0].kind = DiamondComboStepKind::HoldStart;
    p.steps[0].control = DiamondLogicalControl::LeftTrigger;
    p.steps[1].enabled = true;
    p.steps[1].kind = DiamondComboStepKind::Pulse;
    p.steps[1].control = DiamondLogicalControl::South;
    p.steps[1].durationMs = 35;
    p.steps[1].intervalMs = 80;
    p.steps[1].repeatCount = 0;
    p.steps[1].logicalMask =
        (1u << static_cast<std::uint8_t>(DiamondLogicalControl::South)) |
        (1u << static_cast<std::uint8_t>(DiamondLogicalControl::LeftTrigger));
    p.steps[1].keyboardKeys[0] = 0x0Du;
    p.steps[1].keyboardModifiers = 0x02u;
    p.steps[1].mouseButtons = 4u; // middle / scroll click
    p.steps[1].mouseWheel = -1;
    p.steps[1].delayAfterMs = 25;
    finalizeDiamondConfigRecord(program);
    assert(validateDiamondConfigRecord(program));
    program.payload.names.comboPrograms[2].steps[1].intervalMs ^= 1u;
    assert(!validateDiamondConfigRecord(program));

    std::cout << "OAG_DIAMOND_CONFIG_RECORD_TESTS=PASS\n";
    return 0;
}
