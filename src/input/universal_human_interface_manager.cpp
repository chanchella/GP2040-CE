#include "input/universal_human_interface_manager.h"

UniversalHumanInterfaceManager&
UniversalHumanInterfaceManager::getInstance() {
    static UniversalHumanInterfaceManager instance;
    return instance;
}

UniversalHumanInterfaceManager::UniversalHumanInterfaceManager() {
    critical_section_init(&lock);

    for (uint8_t slot = 0; slot < UNIVERSAL_HID_SLOT_COUNT; slot++) {
        resetKeyboardSlotUnlocked(slot);
        resetMouseSlotUnlocked(slot);
    }
}

void UniversalHumanInterfaceManager::resetKeyboardSlotUnlocked(
    uint8_t slot
) {
    const uint32_t nextGeneration =
        keyboardSlots[slot].generation + 1;

    keyboardSlots[slot] = KeyboardSlot {};
    keyboardSlots[slot].generation = nextGeneration;
}

void UniversalHumanInterfaceManager::resetMouseSlotUnlocked(
    uint8_t slot
) {
    const uint32_t nextGeneration =
        mouseSlots[slot].generation + 1;

    mouseSlots[slot] = MouseSlot {};
    mouseSlots[slot].generation = nextGeneration;
}

void UniversalHumanInterfaceManager::resetAll() {
    critical_section_enter_blocking(&lock);

    for (uint8_t slot = 0; slot < UNIVERSAL_HID_SLOT_COUNT; slot++) {
        resetKeyboardSlotUnlocked(slot);
        resetMouseSlotUnlocked(slot);
    }

    critical_section_exit(&lock);
}

bool UniversalHumanInterfaceManager::connectKeyboard(
    uint8_t slot,
    UniversalHumanInterfaceSource source,
    UniversalTransport transport,
    UniversalProtocol protocol,
    uint16_t vid,
    uint16_t pid,
    uint8_t devAddr,
    uint8_t instance
) {
    if (
        !validSlot(slot) ||
        source == UniversalHumanInterfaceSource::NONE
    ) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    KeyboardSlot& target = keyboardSlots[slot];

    const bool sameIdentity =
        target.connected &&
        target.source == source &&
        target.transport == transport &&
        target.protocol == protocol &&
        target.vid == vid &&
        target.pid == pid &&
        target.devAddr == devAddr &&
        target.instance == instance;

    if (!sameIdentity) {
        const uint32_t nextGeneration = target.generation + 1;

        target = KeyboardSlot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = transport;
        target.protocol = protocol;
        target.vid = vid;
        target.pid = pid;
        target.devAddr = devAddr;
        target.instance = instance;
    }

    critical_section_exit(&lock);
    return true;
}

bool UniversalHumanInterfaceManager::connectMouse(
    uint8_t slot,
    UniversalHumanInterfaceSource source,
    UniversalTransport transport,
    UniversalProtocol protocol,
    uint16_t vid,
    uint16_t pid,
    uint8_t devAddr,
    uint8_t instance
) {
    if (
        !validSlot(slot) ||
        source == UniversalHumanInterfaceSource::NONE
    ) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    MouseSlot& target = mouseSlots[slot];

    const bool sameIdentity =
        target.connected &&
        target.source == source &&
        target.transport == transport &&
        target.protocol == protocol &&
        target.vid == vid &&
        target.pid == pid &&
        target.devAddr == devAddr &&
        target.instance == instance;

    if (!sameIdentity) {
        const uint32_t nextGeneration = target.generation + 1;

        target = MouseSlot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = transport;
        target.protocol = protocol;
        target.vid = vid;
        target.pid = pid;
        target.devAddr = devAddr;
        target.instance = instance;
    }

    critical_section_exit(&lock);
    return true;
}

uint8_t UniversalHumanInterfaceManager::claimUsbKeyboardSlot(
    UniversalHumanInterfaceSource source,
    UniversalTransport transport,
    uint16_t vid,
    uint16_t pid,
    uint8_t devAddr,
    uint8_t instance
) {
    if (source == UniversalHumanInterfaceSource::NONE) {
        return UNIVERSAL_HID_SLOT_INVALID;
    }

    critical_section_enter_blocking(&lock);

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        const KeyboardSlot& current = keyboardSlots[slot];

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

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        if (keyboardSlots[slot].connected) {
            continue;
        }

        const uint32_t nextGeneration =
            keyboardSlots[slot].generation + 1;

        KeyboardSlot& target = keyboardSlots[slot];
        target = KeyboardSlot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = transport;
        target.protocol = UniversalProtocol::HID_KEYBOARD;
        target.vid = vid;
        target.pid = pid;
        target.devAddr = devAddr;
        target.instance = instance;

        critical_section_exit(&lock);
        return slot;
    }

    critical_section_exit(&lock);
    return UNIVERSAL_HID_SLOT_INVALID;
}

uint8_t UniversalHumanInterfaceManager::claimUsbMouseSlot(
    UniversalHumanInterfaceSource source,
    UniversalTransport transport,
    uint16_t vid,
    uint16_t pid,
    uint8_t devAddr,
    uint8_t instance
) {
    if (source == UniversalHumanInterfaceSource::NONE) {
        return UNIVERSAL_HID_SLOT_INVALID;
    }

    critical_section_enter_blocking(&lock);

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        const MouseSlot& current = mouseSlots[slot];

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

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        if (mouseSlots[slot].connected) {
            continue;
        }

        const uint32_t nextGeneration =
            mouseSlots[slot].generation + 1;

        MouseSlot& target = mouseSlots[slot];
        target = MouseSlot {};
        target.generation = nextGeneration;
        target.connected = true;
        target.source = source;
        target.transport = transport;
        target.protocol = UniversalProtocol::HID_MOUSE;
        target.vid = vid;
        target.pid = pid;
        target.devAddr = devAddr;
        target.instance = instance;

        critical_section_exit(&lock);
        return slot;
    }

    critical_section_exit(&lock);
    return UNIVERSAL_HID_SLOT_INVALID;
}

void UniversalHumanInterfaceManager::disconnectKeyboard(
    uint8_t slot
) {
    if (!validSlot(slot)) {
        return;
    }

    critical_section_enter_blocking(&lock);
    resetKeyboardSlotUnlocked(slot);
    critical_section_exit(&lock);
}

void UniversalHumanInterfaceManager::disconnectMouse(uint8_t slot) {
    if (!validSlot(slot)) {
        return;
    }

    critical_section_enter_blocking(&lock);
    resetMouseSlotUnlocked(slot);
    critical_section_exit(&lock);
}

void UniversalHumanInterfaceManager::releaseUsbKeyboardDevice(
    UniversalHumanInterfaceSource source,
    uint8_t devAddr
) {
    critical_section_enter_blocking(&lock);

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        const KeyboardSlot& current = keyboardSlots[slot];

        if (
            current.connected &&
            current.source == source &&
            current.devAddr == devAddr
        ) {
            resetKeyboardSlotUnlocked(slot);
        }
    }

    critical_section_exit(&lock);
}

void UniversalHumanInterfaceManager::releaseUsbMouseDevice(
    UniversalHumanInterfaceSource source,
    uint8_t devAddr
) {
    critical_section_enter_blocking(&lock);

    for (
        uint8_t slot = UNIVERSAL_HID_SLOT_USB_1;
        slot <= UNIVERSAL_HID_SLOT_USB_3;
        slot++
    ) {
        const MouseSlot& current = mouseSlots[slot];

        if (
            current.connected &&
            current.source == source &&
            current.devAddr == devAddr
        ) {
            resetMouseSlotUnlocked(slot);
        }
    }

    critical_section_exit(&lock);
}

bool UniversalHumanInterfaceManager::publishKeyboard(
    uint8_t slot,
    UniversalKeyboardState const& state
) {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    KeyboardSlot& target = keyboardSlots[slot];

    if (!target.connected) {
        critical_section_exit(&lock);
        return false;
    }

    target.state = state;
    target.hasReport = true;
    target.generation++;

    critical_section_exit(&lock);
    return true;
}

bool UniversalHumanInterfaceManager::publishMouse(
    uint8_t slot,
    UniversalMouseState const& state
) {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    MouseSlot& target = mouseSlots[slot];

    if (!target.connected) {
        critical_section_exit(&lock);
        return false;
    }

    target.state = state;
    target.hasReport = true;
    target.generation++;

    critical_section_exit(&lock);
    return true;
}

bool UniversalHumanInterfaceManager::snapshotKeyboard(
    uint8_t slot,
    UniversalKeyboardSlotSnapshot& out
) const {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    const KeyboardSlot& source = keyboardSlots[slot];
    out.connected = source.connected;
    out.hasReport = source.hasReport;
    out.source = source.source;
    out.transport = source.transport;
    out.protocol = source.protocol;
    out.vid = source.vid;
    out.pid = source.pid;
    out.devAddr = source.devAddr;
    out.instance = source.instance;
    out.generation = source.generation;
    out.state = source.state;

    critical_section_exit(&lock);
    return true;
}

bool UniversalHumanInterfaceManager::snapshotMouse(
    uint8_t slot,
    UniversalMouseSlotSnapshot& out
) const {
    if (!validSlot(slot)) {
        return false;
    }

    critical_section_enter_blocking(&lock);

    const MouseSlot& source = mouseSlots[slot];
    out.connected = source.connected;
    out.hasReport = source.hasReport;
    out.source = source.source;
    out.transport = source.transport;
    out.protocol = source.protocol;
    out.vid = source.vid;
    out.pid = source.pid;
    out.devAddr = source.devAddr;
    out.instance = source.instance;
    out.generation = source.generation;
    out.state = source.state;

    critical_section_exit(&lock);
    return true;
}
