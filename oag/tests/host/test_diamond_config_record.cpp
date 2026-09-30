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

    static_assert(sizeof(DiamondConfigRecord) <= 4096);
    std::cout << "OAG_DIAMOND_CONFIG_RECORD_TESTS=PASS\n";
    return 0;
}
