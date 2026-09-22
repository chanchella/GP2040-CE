#ifndef _UNIVERSAL_FEEDBACK_MANAGER_H_
#define _UNIVERSAL_FEEDBACK_MANAGER_H_

#include <stdint.h>

#include "pico/sync.h"

static constexpr uint8_t UNIVERSAL_FEEDBACK_SLOT_COUNT = 4;

struct UniversalRumbleSnapshot {
    uint8_t strong = 0;
    uint8_t weak = 0;
    uint32_t generation = 0;
};

class UniversalFeedbackManager {
public:
    UniversalFeedbackManager(UniversalFeedbackManager const&) = delete;
    void operator=(UniversalFeedbackManager const&) = delete;

    static UniversalFeedbackManager& getInstance();

    void resetAll();

    bool setRumble(
        uint8_t slot,
        uint8_t strong,
        uint8_t weak
    );

    bool snapshot(
        uint8_t slot,
        UniversalRumbleSnapshot& out
    ) const;

private:
    UniversalFeedbackManager();

    struct Slot {
        uint8_t strong = 0;
        uint8_t weak = 0;
        uint32_t generation = 0;
    };

    mutable critical_section_t lock;
    Slot slots[UNIVERSAL_FEEDBACK_SLOT_COUNT];

    static bool validSlot(uint8_t slot) {
        return slot < UNIVERSAL_FEEDBACK_SLOT_COUNT;
    }
};

#define UFEEDBACK UniversalFeedbackManager::getInstance()

#endif
