#ifndef _UNIVERSAL_FEEDBACK_MANAGER_H_
#define _UNIVERSAL_FEEDBACK_MANAGER_H_

#include <stdint.h>
#include "pico/sync.h"

static constexpr uint8_t UNIVERSAL_FEEDBACK_SLOT_COUNT = 4;

struct UniversalFeedbackSlotSnapshot {
    bool valid = false;
    uint8_t leftMotor = 0;
    uint8_t rightMotor = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    uint32_t generation = 0;
};

class UniversalFeedbackManager {
public:
    UniversalFeedbackManager(UniversalFeedbackManager const&) = delete;
    void operator=(UniversalFeedbackManager const&) = delete;
    static UniversalFeedbackManager& getInstance();
    void resetAll();
    void invalidate(uint8_t slot);
    bool publish(uint8_t slot, uint8_t leftMotor, uint8_t rightMotor,
                 uint8_t leftTrigger = 0, uint8_t rightTrigger = 0);
    bool snapshot(uint8_t slot, UniversalFeedbackSlotSnapshot& out) const;
private:
    UniversalFeedbackManager();
    struct Slot {
        bool valid = false;
        uint8_t leftMotor = 0;
        uint8_t rightMotor = 0;
        uint8_t leftTrigger = 0;
        uint8_t rightTrigger = 0;
        uint32_t generation = 0;
    };
    mutable critical_section_t lock;
    Slot slots[UNIVERSAL_FEEDBACK_SLOT_COUNT] {};
    static bool validSlot(uint8_t slot) { return slot < UNIVERSAL_FEEDBACK_SLOT_COUNT; }
    void invalidateUnlocked(uint8_t slot);
};
#define UFEEDBACK UniversalFeedbackManager::getInstance()
#endif
