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
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        resetSlot(i);
    }
}

void UniversalXInputHostAddon::preprocess() {
    // G1C hot-plug recovery:
    // A freshly mounted XInput endpoint can occasionally fail its first
    // receive arm because enumeration/configuration has only just completed.
    // If an endpoint is mounted and currently idle, retry the receive arm.
    // Once a transfer is pending, tuh_xinput_ready() becomes false, so this
    // does not queue duplicate transfers.
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (
            slots[i].mounted &&
            tuh_xinput_mounted(slots[i].devAddr, slots[i].instance) &&
            tuh_xinput_ready(slots[i].devAddr, slots[i].instance)
        ) {
            tuh_xinput_receive_report(slots[i].devAddr, slots[i].instance);
        }
    }

    // Route USB/XInput logical slot 0 to the existing GP2040 output.
    // Slots 1 and 2 remain independent and reserved for later multi-output routing.
    if (!slots[0].mounted || !slots[0].hasReport) {
        return;
    }

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    gamepad->hasAnalogTriggers = true;
    gamepad->hasLeftAnalogStick = true;
    gamepad->hasRightAnalogStick = true;
    gamepad->state = slots[0].state;
}

void UniversalXInputHostAddon::resetSlot(uint8_t slot) {
    if (slot >= SLOT_COUNT) {
        return;
    }
    slots[slot] = XInputSlot {};
}

int8_t UniversalXInputHostAddon::findSlot(uint8_t devAddr, uint8_t instance) const {
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].mounted &&
            slots[i].devAddr == devAddr &&
            slots[i].instance == instance) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

int8_t UniversalXInputHostAddon::allocateSlot(uint8_t devAddr, uint8_t instance, uint8_t subtype) {
    const int8_t existing = findSlot(devAddr, instance);
    if (existing >= 0) {
        return existing;
    }

    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (!slots[i].mounted) {
            resetSlot(i);
            slots[i].mounted = true;
            slots[i].devAddr = devAddr;
            slots[i].instance = instance;
            slots[i].subtype = subtype;
            return static_cast<int8_t>(i);
        }
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

    // Normal Xbox 360 gameplay interfaces expose a non-zero subtype.
    // T29 has been observed as 045E:028E; accept instance 0 as a safe
    // compatibility fallback even if a clone omits the subtype.
    const bool t29Fallback =
        hasVidPid &&
        vid == 0x045E &&
        pid == 0x028E &&
        instance == 0;

    // Some compatible wired controllers do not expose a useful subtype.
    // Instance 0 is the normal gameplay interface; the exact T29 identity
    // is accepted explicitly as well.
    if (subtype == 0 && instance != 0 && !t29Fallback) {
        return;
    }

    allocateSlot(dev_addr, instance, subtype);
}

void UniversalXInputHostAddon::unmount(uint8_t dev_addr) {
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (slots[i].mounted && slots[i].devAddr == dev_addr) {
            resetSlot(i);
        }
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

    slots[slotIndex].state = next;
    slots[slotIndex].hasReport = true;
}

uint16_t UniversalXInputHostAddon::axisX(int16_t value) {
    return static_cast<uint16_t>(static_cast<int32_t>(value) + 32768);
}

uint16_t UniversalXInputHostAddon::axisY(int16_t value) {
    // GP2040's XInput device driver inverts Y on output.
    // Store the host value in GP2040's internal orientation.
    return static_cast<uint16_t>(32767 - static_cast<int32_t>(value));
}

bool UniversalXInputHostAddon::parseXbox360Report(
    GamepadState& out,
    uint8_t const* report,
    uint16_t len
) {
    if (report == nullptr || len < 14) {
        return false;
    }

    // The proven T29 parser only requires the XUSB payload-size byte.
    // Some clones vary the first status byte.
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
