#include "output/universal_feedback_manager.h"

UniversalFeedbackManager& UniversalFeedbackManager::getInstance() {
    static UniversalFeedbackManager instance;
    return instance;
}

UniversalFeedbackManager::UniversalFeedbackManager() {
    critical_section_init(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_FEEDBACK_SLOT_COUNT; i++) {
        slots[i] = Slot {};
    }
}

void UniversalFeedbackManager::resetAll() {
    critical_section_enter_blocking(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_FEEDBACK_SLOT_COUNT; i++) {
        const uint32_t nextGeneration = slots[i].generation + 1;
        slots[i] = Slot {};
        slots[i].generation = nextGeneration;
    }

    critical_section_exit(&lock);
}

bool UniversalFeedbackManager::setRumble(
    uint8_t slot,
    uint8_t strong,
    uint8_t weak
) {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    Slot& target = slots[slot];

    if (
        target.strong != strong ||
        target.weak != weak
    ) {
        target.strong = strong;
        target.weak = weak;
        target.generation++;
    }

    critical_section_exit(&lock);
    return true;
}

bool UniversalFeedbackManager::snapshot(
    uint8_t slot,
    UniversalRumbleSnapshot& out
) const {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    out.strong = slots[slot].strong;
    out.weak = slots[slot].weak;
    out.generation = slots[slot].generation;

    critical_section_exit(&lock);
    return true;
}
