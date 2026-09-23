#include "output/universal_feedback_manager.h"

UniversalFeedbackManager& UniversalFeedbackManager::getInstance() {
    static UniversalFeedbackManager instance;
    return instance;
}
UniversalFeedbackManager::UniversalFeedbackManager() { critical_section_init(&lock); }
void UniversalFeedbackManager::invalidateUnlocked(uint8_t slot) {
    const uint32_t nextGeneration = slots[slot].generation + 1;
    slots[slot] = Slot {};
    slots[slot].generation = nextGeneration;
}
void UniversalFeedbackManager::resetAll() {
    critical_section_enter_blocking(&lock);
    for (uint8_t slot = 0; slot < UNIVERSAL_FEEDBACK_SLOT_COUNT; slot++) invalidateUnlocked(slot);
    critical_section_exit(&lock);
}
void UniversalFeedbackManager::invalidate(uint8_t slot) {
    if (!validSlot(slot)) return;
    critical_section_enter_blocking(&lock);
    invalidateUnlocked(slot);
    critical_section_exit(&lock);
}
bool UniversalFeedbackManager::publish(uint8_t slot, uint8_t leftMotor, uint8_t rightMotor,
                                       uint8_t leftTrigger, uint8_t rightTrigger) {
    if (!validSlot(slot)) return false;
    critical_section_enter_blocking(&lock);
    Slot& target = slots[slot];
    const bool changed = !target.valid || target.leftMotor != leftMotor ||
        target.rightMotor != rightMotor || target.leftTrigger != leftTrigger ||
        target.rightTrigger != rightTrigger;
    if (changed) {
        target.valid = true;
        target.leftMotor = leftMotor;
        target.rightMotor = rightMotor;
        target.leftTrigger = leftTrigger;
        target.rightTrigger = rightTrigger;
        target.generation++;
    }
    critical_section_exit(&lock);
    return changed;
}
bool UniversalFeedbackManager::snapshot(uint8_t slot, UniversalFeedbackSlotSnapshot& out) const {
    if (!validSlot(slot)) return false;
    critical_section_enter_blocking(&lock);
    const Slot& source = slots[slot];
    out.valid = source.valid;
    out.leftMotor = source.leftMotor;
    out.rightMotor = source.rightMotor;
    out.leftTrigger = source.leftTrigger;
    out.rightTrigger = source.rightTrigger;
    out.generation = source.generation;
    critical_section_exit(&lock);
    return true;
}
