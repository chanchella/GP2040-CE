#include "addons/universal_xinput_host.h"

#include "peripheralmanager.h"
#include "storagemanager.h"
#include "drivers/shared/xinput_host.h"
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
    uint16_t vid,
    uint16_t pid
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

        if (
            !UINPUT.connect(
                globalSlot,
                UniversalInputSource::USB_XINPUT,
                vid,
                pid,
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

    // Exact compatibility fallback for the user's T29/XUSB identity.
    const bool t29Fallback =
        hasVidPid &&
        vid == 0x045E &&
        pid == 0x028E &&
        instance == 0;

    // Some compatible wired controllers omit a useful subtype.
    // Instance 0 is accepted as the normal gameplay interface.
    if (subtype == 0 && instance != 0 && !t29Fallback) {
        return;
    }

    allocateSlot(
        dev_addr,
        instance,
        subtype,
        hasVidPid ? vid : 0,
        hasVidPid ? pid : 0
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
    if (!parseXbox360Report(next, report, len)) {
        return;
    }

    const uint8_t globalSlot = slots[slotIndex].globalSlot;
    if (globalSlot == UNIVERSAL_INPUT_SLOT_INVALID) {
        return;
    }

    UINPUT.publish(globalSlot, next);
}

uint16_t UniversalXInputHostAddon::axisX(int16_t value) {
    return static_cast<uint16_t>(
        static_cast<int32_t>(value) + 32768
    );
}

uint16_t UniversalXInputHostAddon::axisY(int16_t value) {
    // GP2040's XInput device driver inverts Y on output.
    // Store the host value in GP2040's internal orientation.
    return static_cast<uint16_t>(
        32767 - static_cast<int32_t>(value)
    );
}

bool UniversalXInputHostAddon::parseXbox360Report(
    GamepadState& out,
    uint8_t const* report,
    uint16_t len
) {
    if (report == nullptr || len < 14) {
        return false;
    }

    // Proven T29 parser requirement.
    if (report[1] != 0x14) {
        return false;
    }

    const uint16_t buttons =
        static_cast<uint16_t>(report[2]) |
        (static_cast<uint16_t>(report[3]) << 8);

    if (buttons & 0x0001) out.dpad |= GAMEPAD_MASK_UP;
    if (buttons & 0x0002) out.dpad |= GAMEPAD_MASK_DOWN;
    if (buttons & 0x0004) out.dpad |= GAMEPAD_MASK_LEFT;
    if (buttons & 0x0008) out.dpad |= GAMEPAD_MASK_RIGHT;

    if (buttons & 0x0010) out.buttons |= GAMEPAD_MASK_S2;
    if (buttons & 0x0020) out.buttons |= GAMEPAD_MASK_S1;
    if (buttons & 0x0040) out.buttons |= GAMEPAD_MASK_L3;
    if (buttons & 0x0080) out.buttons |= GAMEPAD_MASK_R3;
    if (buttons & 0x0100) out.buttons |= GAMEPAD_MASK_L1;
    if (buttons & 0x0200) out.buttons |= GAMEPAD_MASK_R1;
    if (buttons & 0x0400) out.buttons |= GAMEPAD_MASK_A1;

    if (buttons & 0x1000) out.buttons |= GAMEPAD_MASK_B1;
    if (buttons & 0x2000) out.buttons |= GAMEPAD_MASK_B2;
    if (buttons & 0x4000) out.buttons |= GAMEPAD_MASK_B3;
    if (buttons & 0x8000) out.buttons |= GAMEPAD_MASK_B4;

    out.lt = report[4];
    out.rt = report[5];

    if (out.lt != 0) out.buttons |= GAMEPAD_MASK_L2;
    if (out.rt != 0) out.buttons |= GAMEPAD_MASK_R2;

    auto readS16 = [report](uint8_t offset) -> int16_t {
        const uint16_t raw =
            static_cast<uint16_t>(report[offset]) |
            (static_cast<uint16_t>(report[offset + 1]) << 8);

        return static_cast<int16_t>(raw);
    };

    out.lx = axisX(readS16(6));
    out.ly = axisY(readS16(8));
    out.rx = axisX(readS16(10));
    out.ry = axisY(readS16(12));
    out.dpadOriginal = out.dpad;

    return true;
}
