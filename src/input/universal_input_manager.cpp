#include "input/universal_input_manager.h"

UniversalInputManager& UniversalInputManager::getInstance() {
    static UniversalInputManager instance;
    return instance;
}

UniversalInputManager::UniversalInputManager() {
    critical_section_init(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_INPUT_SLOT_COUNT; i++) {
        resetSlotUnlocked(i);
    }
}

void UniversalInputManager::resetSlotUnlocked(uint8_t slot) {
    const uint32_t nextGeneration = slots[slot].generation + 1;

    slots[slot] = Slot {};
    slots[slot].generation = nextGeneration;
}

void UniversalInputManager::resetAll() {
    critical_section_enter_blocking(&lock);

    for (uint8_t i = 0; i < UNIVERSAL_INPUT_SLOT_COUNT; i++) {
        resetSlotUnlocked(i);
    }

    critical_section_exit(&lock);
}

bool UniversalInputManager::connect(
    uint8_t slot,
    UniversalInputSource source,
    uint16_t vid,
    uint16_t pid,
    uint8_t devAddr,
    uint8_t instance
) {
    UniversalDeviceMatch match {};
    match.vid = vid;
    match.pid = pid;

    return connectClassified(
        slot,
        source,
        match,
        devAddr,
        instance
    );
}

bool UniversalInputManager::connectClassified(
    uint8_t slot,
    UniversalInputSource source,
    UniversalDeviceMatch const& match,
    uint8_t devAddr,
    uint8_t instance
) {
    if (!validSlot(slot) || source == UniversalInputSource::NONE) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    Slot& target = slots[slot];

    const bool sameIdentity =
        target.connected &&
        target.source == source &&
        target.transport == match.transport &&
        target.deviceClass == match.deviceClass &&
        target.protocol == match.protocol &&
        target.driverFamily == match.driverFamily &&
        target.profile == match.profile &&
        target.quirks == match.quirks &&
        target.capabilities == match.capabilities &&
        target.verifiedCapabilities == match.verifiedCapabilities &&
        target.vid == match.vid &&
        target.pid == match.pid &&
        target.devAddr == devAddr &&
        target.instance == instance;

    if (!sameIdentity) {
        const uint32_t nextGeneration = target.generation + 1;

        target = Slot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = match.transport;
        target.deviceClass = match.deviceClass;
        target.protocol = match.protocol;
        target.driverFamily = match.driverFamily;
        target.profile = match.profile;
        target.quirks = match.quirks;
        target.capabilities = match.capabilities;
        target.verifiedCapabilities = match.verifiedCapabilities;
        target.vid = match.vid;
        target.pid = match.pid;
        target.devAddr = devAddr;
        target.instance = instance;
    }

    critical_section_exit(&lock);
    return true;
}

uint8_t UniversalInputManager::claimUsbSlot(
    UniversalInputSource source,
    UniversalDeviceMatch const& match,
    uint8_t devAddr,
    uint8_t instance
) {
    if (source == UniversalInputSource::NONE) {
        return UNIVERSAL_INPUT_SLOT_INVALID;
    }

    critical_section_enter_blocking(&lock);

    // Preserve an existing assignment for the same transport endpoint.
    for (
        uint8_t slot = UNIVERSAL_INPUT_SLOT_USB_1;
        slot <= UNIVERSAL_INPUT_SLOT_USB_3;
        slot++
    ) {
        Slot const& current = slots[slot];

        if (
            current.connected &&
            current.source == source &&
            current.devAddr == devAddr &&
            current.instance == instance
        ) {
            critical_section_exit(&lock);
            return slot;
        }
    }

    // First-free allocation keeps different USB transport drivers from
    // colliding with one another.
    for (
        uint8_t slot = UNIVERSAL_INPUT_SLOT_USB_1;
        slot <= UNIVERSAL_INPUT_SLOT_USB_3;
        slot++
    ) {
        Slot& target = slots[slot];

        if (target.connected) {
            continue;
        }

        const uint32_t nextGeneration = target.generation + 1;

        target = Slot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = match.transport;
        target.deviceClass = match.deviceClass;
        target.protocol = match.protocol;
        target.driverFamily = match.driverFamily;
        target.profile = match.profile;
        target.quirks = match.quirks;
        target.capabilities = match.capabilities;
        target.verifiedCapabilities = match.verifiedCapabilities;
        target.vid = match.vid;
        target.pid = match.pid;
        target.devAddr = devAddr;
        target.instance = instance;

        critical_section_exit(&lock);
        return slot;
    }

    critical_section_exit(&lock);
    return UNIVERSAL_INPUT_SLOT_INVALID;
}

void UniversalInputManager::releaseUsbDevice(
    UniversalInputSource source,
    uint8_t devAddr
) {
    if (source == UniversalInputSource::NONE) {
        return;
    }

    critical_section_enter_blocking(&lock);

    for (
        uint8_t slot = UNIVERSAL_INPUT_SLOT_USB_1;
        slot <= UNIVERSAL_INPUT_SLOT_USB_3;
        slot++
    ) {
        Slot const& current = slots[slot];

        if (
            current.connected &&
            current.source == source &&
            current.devAddr == devAddr
        ) {
            resetSlotUnlocked(slot);
        }
    }

    critical_section_exit(&lock);
}

void UniversalInputManager::disconnect(uint8_t slot) {
    if (!validSlot(slot)) {
        return;
    }

    critical_section_enter_blocking(&lock);
    resetSlotUnlocked(slot);
    critical_section_exit(&lock);
}

bool UniversalInputManager::publish(
    uint8_t slot,
    GamepadState const& state
) {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    Slot& target = slots[slot];

    if (!target.connected) {
        critical_section_exit(&lock);
        return false;
    }

    target.state = state;
    target.hasReport = true;

    critical_section_exit(&lock);
    return true;
}

bool UniversalInputManager::snapshot(
    uint8_t slot,
    UniversalInputSlotSnapshot& out
) const {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    const Slot& source = slots[slot];

    out.connected = source.connected;
    out.hasReport = source.hasReport;
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
    out.devAddr = source.devAddr;
    out.instance = source.instance;
    out.generation = source.generation;
    out.state = source.state;

    critical_section_exit(&lock);
    return true;
}
