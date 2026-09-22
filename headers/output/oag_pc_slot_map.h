#ifndef _OAG_PC_SLOT_MAP_H_
#define _OAG_PC_SLOT_MAP_H_

#include <stdint.h>

// PC presentation order.
//
// Internal Slot 0 is reserved for Bluetooth. Until Bluetooth is integrated,
// presenting Slot 0 as the first Windows/browser controller leaves the first
// visible pad permanently neutral. Present USB slots first instead while
// preserving the internal deterministic slot namespace.
//
// Physical PC pad 0 <- Universal Output 1
// Physical PC pad 1 <- Universal Output 2
// Physical PC pad 2 <- Universal Output 3
// Physical PC pad 3 <- Universal Output 0 (Bluetooth-reserved)
static constexpr uint8_t OAG_PC_PRESENTATION_SLOT_COUNT = 4;
static constexpr uint8_t OAG_PC_PHYSICAL_TO_LOGICAL[
    OAG_PC_PRESENTATION_SLOT_COUNT
] = {1, 2, 3, 0};

static constexpr uint8_t oagPcLogicalSlot(
    uint8_t physicalSlot
) {
    return
        physicalSlot < OAG_PC_PRESENTATION_SLOT_COUNT
            ? OAG_PC_PHYSICAL_TO_LOGICAL[physicalSlot]
            : 0xFF;
}

#endif
