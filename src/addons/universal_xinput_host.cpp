#include "addons/universal_xinput_host.h"

#include "peripheralmanager.h"
#include "storagemanager.h"
#include "drivers/shared/xinput_host.h"
#include "input/universal_gamepad_parser.h"
#include "tusb.h"

bool UniversalXInputHostAddon::available() {
    return UNIVERSAL_XINPUT_HOST_ENABLED &&
           PeripheralManager::getInstance().isUSBEnabled(0);
}

void UniversalXInputHostAddon::setup() {
    // USB transport owns only global slots 1..3.
    // Slot 0 is reserved for the future Bluetooth transport.
    UINPUT.disconnect(UNIVERSAL_INPUT_SLOT_USB_1);
    UINPUT.disconnect(UNIVERSAL_INPUT_SLOT_USB_2);
    UINPUT.disconnect(UNIVERSAL_INPUT_SLOT_USB_3);

    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        resetSlot(i);
    }
}

void UniversalXInputHostAddon::preprocess() {
    // Input transport only.
    // Keep mounted XInput endpoints armed, but do not touch the
    // GP2040 output state here. G2B owns output routing separately.
    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        if (
            slots[i].mounted &&
            tuh_xinput_mounted(slots[i].devAddr, slots[i].instance) &&
            tuh_xinput_ready(slots[i].devAddr, slots[i].instance)
        ) {
            tuh_xinput_receive_report(slots[i].devAddr, slots[i].instance);
        }
    }
}

void UniversalXInputHostAddon::resetSlot(uint8_t slot) {
    if (slot >= USB_SLOT_COUNT) {
        return;
    }

    slots[slot] = XInputTransportSlot {};
}

int8_t UniversalXInputHostAddon::findSlot(
    uint8_t devAddr,
    uint8_t instance
) const {
    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        if (
            slots[i].mounted &&
            slots[i].devAddr == devAddr &&
            slots[i].instance == instance
        ) {
            return static_cast<int8_t>(i);
        }
    }

    return -1;
}

int8_t UniversalXInputHostAddon::allocateSlot(
    uint8_t devAddr,
    uint8_t instance,
    uint8_t subtype,
    UniversalDeviceMatch const& match
) {
    const int8_t existing = findSlot(devAddr, instance);
    if (existing >= 0) {
        return existing;
    }

    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        if (slots[i].mounted) {
            continue;
        }

        const uint8_t globalSlot =
            static_cast<uint8_t>(UNIVERSAL_INPUT_SLOT_USB_1 + i);

        resetSlot(i);

        slots[i].mounted = true;
        slots[i].devAddr = devAddr;
        slots[i].instance = instance;
        slots[i].subtype = subtype;
        slots[i].globalSlot = globalSlot;
        slots[i].device = match;

        if (
            !UINPUT.connectClassified(
                globalSlot,
                UniversalInputSource::USB_XINPUT,
                match,
                devAddr,
                instance
            )
        ) {
            resetSlot(i);
            return -1;
        }

        return static_cast<int8_t>(i);
    }

    return -1;
}

void UniversalXInputHostAddon::xmount(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t controllerType,
    uint8_t subtype
) {
    if (controllerType != xinput_type_t::XBOX360) {
        return;
    }

    uint16_t vid = 0;
    uint16_t pid = 0;
    const bool hasVidPid = tuh_vid_pid_get(dev_addr, &vid, &pid);

    UniversalUsbProbe probe {};
    probe.vid = hasVidPid ? vid : 0;
    probe.pid = hasVidPid ? pid : 0;

    // xmount() is reached only after the custom XInput host driver has
    // claimed an Xbox 360 gameplay-compatible interface.
    probe.interfaceClass = 0xFF;
    probe.interfaceSubClass = 0x5D;
    probe.interfaceProtocol = 0x01;
    probe.endpointCount = 2;

    const UniversalDeviceMatch match = UDEVREG.classifyUsb(probe);

    const bool exactCompatFallback =
        match.profile == UniversalDeviceProfileId::XUSB_045E_028E_COMPAT &&
        instance == 0;

    // Some compatible wired controllers omit a useful subtype.
    // Instance 0 is accepted as the normal gameplay interface.
    if (subtype == 0 && instance != 0 && !exactCompatFallback) {
        return;
    }

    allocateSlot(
        dev_addr,
        instance,
        subtype,
        match
    );
}

void UniversalXInputHostAddon::unmount(uint8_t dev_addr) {
    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        if (!slots[i].mounted || slots[i].devAddr != dev_addr) {
            continue;
        }

        if (slots[i].globalSlot != UNIVERSAL_INPUT_SLOT_INVALID) {
            UINPUT.disconnect(slots[i].globalSlot);
        }

        resetSlot(i);
    }
}

void UniversalXInputHostAddon::report_received(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* report,
    uint16_t len
) {
    const int8_t slotIndex = findSlot(dev_addr, instance);
    if (slotIndex < 0) {
        return;
    }

    GamepadState next {};
    if (
        !UGAMEPADPARSER.parse(
            slots[slotIndex].device,
            report,
            len,
            next
        )
    ) {
        return;
    }

    const uint8_t globalSlot = slots[slotIndex].globalSlot;
    if (globalSlot == UNIVERSAL_INPUT_SLOT_INVALID) {
        return;
    }

    UINPUT.publish(globalSlot, next);
}

