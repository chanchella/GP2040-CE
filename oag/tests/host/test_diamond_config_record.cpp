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

    // V3 spans multiple flash sectors to persist OAG names and combo timing.
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

    std::cout << "OAG_DIAMOND_CONFIG_RECORD_TESTS=PASS\n";
    return 0;
}
