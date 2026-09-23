#include "output/universal_output_manager.h"
#include "output/universal_feedback_manager.h"

UniversalOutputManager& UniversalOutputManager::getInstance() {
    static UniversalOutputManager instance;
    return instance;
}

UniversalOutputManager::UniversalOutputManager() {
    critical_section_init(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_OUTPUT_SLOT_COUNT; i++) {
        slots[i] = Slot {};
    }
}

void UniversalOutputManager::clearSlotUnlocked(uint8_t slot) {
    const uint32_t nextGeneration = slots[slot].generation + 1;

    slots[slot] = Slot {};
    slots[slot].generation = nextGeneration;
    UFEEDBACK.invalidate(slot);
}

void UniversalOutputManager::resetAll() {
    critical_section_enter_blocking(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_OUTPUT_SLOT_COUNT; i++) {
        clearSlotUnlocked(i);
    }

    critical_section_exit(&lock);
}

void UniversalOutputManager::publishFromInputUnlocked(
    uint8_t outputSlot,
    uint8_t inputSlot,
    UniversalInputSlotSnapshot const& input
) {
    Slot& target = slots[outputSlot];

    const bool identityChanged =
        !target.connected ||
        target.inputSlot != inputSlot ||
        target.source != input.source ||
        target.transport != input.transport ||
        target.deviceClass != input.deviceClass ||
        target.protocol != input.protocol ||
        target.driverFamily != input.driverFamily ||
        target.profile != input.profile ||
        target.quirks != input.quirks ||
        target.capabilities != input.capabilities ||
        target.verifiedCapabilities != input.verifiedCapabilities ||
        target.vid != input.vid ||
        target.pid != input.pid ||
        target.inputGeneration != input.generation;

    if (identityChanged) {
        const uint32_t nextGeneration = target.generation + 1;

        target = Slot {};
        target.generation = nextGeneration;
        UFEEDBACK.invalidate(outputSlot);
    }

    target.connected = input.connected;
    target.hasReport = input.hasReport;
    target.inputSlot = inputSlot;
    target.source = input.source;
    target.transport = input.transport;
    target.deviceClass = input.deviceClass;
    target.protocol = input.protocol;
    target.driverFamily = input.driverFamily;
    target.profile = input.profile;
    target.quirks = input.quirks;
    target.capabilities = input.capabilities;
    target.verifiedCapabilities = input.verifiedCapabilities;
    target.vid = input.vid;
    target.pid = input.pid;
    target.inputGeneration = input.generation;
    target.state = input.state;
}

void UniversalOutputManager::syncFromInputs() {
    UniversalInputSlotSnapshot inputSnapshots[UNIVERSAL_INPUT_SLOT_COUNT] {};

    for (uint8_t i = 0; i < UNIVERSAL_INPUT_SLOT_COUNT; i++) {
        UINPUT.snapshot(i, inputSnapshots[i]);
    }

    critical_section_enter_blocking(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_OUTPUT_SLOT_COUNT; i++) {
        const UniversalInputSlotSnapshot& input = inputSnapshots[i];

        if (!input.connected) {
            if (slots[i].connected) {
                clearSlotUnlocked(i);
            }
            continue;
        }

        publishFromInputUnlocked(i, i, input);
    }

    critical_section_exit(&lock);
}

bool UniversalOutputManager::snapshot(
    uint8_t outputSlot,
    UniversalOutputSlotSnapshot& out
) const {
    if (!validSlot(outputSlot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    const Slot& source = slots[outputSlot];

    out.connected = source.connected;
    out.hasReport = source.hasReport;
    out.inputSlot = source.inputSlot;
    out.source = source.source;
    out.transport = source.transport;
    out.deviceClass = source.deviceClass;
    out.protocol = source.protocol;
    out.driverFamily = source.driverFamily;
    out.profile = source.profile;
    out.quirks = source.quirks;
    out.capabilities = source.capabilities;
    out.verifiedCapabilities = source.verifiedCapabilities;
    out.vid = source.vid;
    out.pid = source.pid;
    out.generation = source.generation;
    out.inputGeneration = source.inputGeneration;
    out.state = source.state;

    critical_section_exit(&lock);
    return true;
}
