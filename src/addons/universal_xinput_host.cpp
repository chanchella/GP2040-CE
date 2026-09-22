#include "addons/universal_xinput_host.h"

#include "peripheralmanager.h"
#include "drivers/shared/xinput_host.h"
#include "input/universal_gamepad_parser.h"
#include "output/universal_feedback_manager.h"
#include "tusb.h"

namespace {

static const uint8_t XONE_POWER_ON[] = {
    0x05, 0x20, 0x00, 0x01, 0x00
};

static const uint8_t XONE_S_INIT[] = {
    0x05, 0x20, 0x00, 0x0F, 0x06
};

static const uint8_t XONE_EXTRA_INPUT[] = {
    0x4D, 0x10, 0x01,
    0x02, 0x07, 0x00
};

static const uint8_t XONE_LED_ON[] = {
    0x0A, 0x20, 0x00,
    0x03, 0x00, 0x01, 0x14
};

static const uint8_t XONE_AUTH_DONE[] = {
    0x06, 0x20, 0x00, 0x02, 0x01, 0x00
};

} // namespace

bool UniversalXInputHostAddon::available() {
    return UNIVERSAL_XINPUT_HOST_ENABLED &&
           PeripheralManager::getInstance().isUSBEnabled(0);
}

void UniversalXInputHostAddon::setup() {
    // Release only slots owned by this transport family. Slot 0 remains
    // reserved for the future Bluetooth transport.
    UINPUT.releaseUsbDevice(UniversalInputSource::USB_XINPUT, 0);
    UINPUT.releaseUsbDevice(UniversalInputSource::USB_XGIP, 0);

    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        resetSlot(i);
    }
}

void UniversalXInputHostAddon::preprocess() {
    for (uint8_t i = 0; i < USB_SLOT_COUNT; i++) {
        if (!slots[i].mounted) {
            continue;
        }

        // Xbox One / Series wired controllers require a short normal GIP
        // initialization exchange before they begin ordinary input traffic.
        serviceXgipInit(i);
        serviceRumble(i);

        // Retry/maintain the gameplay IN endpoint. The low-level XInput host
        // also re-arms on successful completion; this path recovers idle/busy
        // transitions without touching output routing.
        if (
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
    uint8_t controllerType,
    uint8_t subtype,
    UniversalInputSource source,
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
            UINPUT.claimUsbSlot(
                source,
                match,
                devAddr,
                instance
            );

        if (globalSlot == UNIVERSAL_INPUT_SLOT_INVALID) {
            return -1;
        }

        resetSlot(i);

        slots[i].mounted = true;
        slots[i].devAddr = devAddr;
        slots[i].instance = instance;
        slots[i].controllerType = controllerType;
        slots[i].subtype = subtype;
        slots[i].globalSlot = globalSlot;
        slots[i].source = source;
        slots[i].device = match;

        if (controllerType == xinput_type_t::XBOXONE) {
            restartXgipInit(i);
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
    if (
        controllerType != xinput_type_t::XBOX360 &&
        controllerType != xinput_type_t::XBOXONE
    ) {
        return;
    }

    uint16_t vid = 0;
    uint16_t pid = 0;
    const bool hasVidPid = tuh_vid_pid_get(dev_addr, &vid, &pid);

    UniversalUsbProbe probe {};
    probe.vid = hasVidPid ? vid : 0;
    probe.pid = hasVidPid ? pid : 0;
    probe.interfaceClass = 0xFF;
    probe.endpointCount = 2;

    UniversalInputSource source = UniversalInputSource::USB_XINPUT;

    if (controllerType == xinput_type_t::XBOXONE) {
        probe.interfaceSubClass = 0x47;
        probe.interfaceProtocol = 0xD0;
        source = UniversalInputSource::USB_XGIP;
    } else {
        probe.interfaceSubClass = 0x5D;
        probe.interfaceProtocol = 0x01;
    }

    const UniversalDeviceMatch match = UDEVREG.classifyUsb(probe);

    if (!match.recognized) {
        return;
    }

    if (controllerType == xinput_type_t::XBOX360) {
        const bool exactCompatFallback =
            match.profile == UniversalDeviceProfileId::XUSB_045E_028E_COMPAT &&
            instance == 0;

        // Some compatible wired controllers omit a useful subtype.
        if (subtype == 0 && instance != 0 && !exactCompatFallback) {
            return;
        }
    }

    allocateSlot(
        dev_addr,
        instance,
        controllerType,
        subtype,
        source,
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
    if (slotIndex < 0 || report == nullptr || len == 0) {
        return;
    }

    XInputTransportSlot& slot = slots[slotIndex];

    if (slot.device.protocol == UniversalProtocol::XGIP_XBOX_ONE) {
        // ANNOUNCE: restart the standard wired-controller init exchange.
        if (report[0] == 0x02) {
            restartXgipInit(static_cast<uint8_t>(slotIndex));
            return;
        }

        // Guide is delivered separately from the normal 0x20 state packet.
        if (report[0] == 0x07 && len >= 5) {
            if (report[4] == 0x01) {
                slot.state.buttons |= GAMEPAD_MASK_A1;
            } else {
                slot.state.buttons &= ~GAMEPAD_MASK_A1;
            }

            UINPUT.publish(slot.globalSlot, slot.state);
            return;
        }
    }

    GamepadState next {};

    if (
        !UGAMEPADPARSER.parse(
            slot.device,
            report,
            len,
            next
        )
    ) {
        return;
    }

    // XGIP Guide state is transported in a separate virtual-key packet.
    if (
        slot.device.protocol == UniversalProtocol::XGIP_XBOX_ONE &&
        (slot.state.buttons & GAMEPAD_MASK_A1)
    ) {
        next.buttons |= GAMEPAD_MASK_A1;
    }

    slot.state = next;

    if (slot.globalSlot != UNIVERSAL_INPUT_SLOT_INVALID) {
        UINPUT.publish(slot.globalSlot, slot.state);
    }
}

void UniversalXInputHostAddon::report_sent(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* report,
    uint16_t len
) {
    (void)report;
    (void)len;

    const int8_t slotIndex = findSlot(dev_addr, instance);
    if (slotIndex < 0) {
        return;
    }

    XInputTransportSlot& slot = slots[slotIndex];

    if (
        slot.device.protocol != UniversalProtocol::XGIP_XBOX_ONE ||
        !slot.xgipTxPending
    ) {
        return;
    }

    slot.xgipTxPending = false;
    advanceXgipInit(static_cast<uint8_t>(slotIndex));
}

void UniversalXInputHostAddon::serviceRumble(
    uint8_t localSlot
) {
    if (
        localSlot >= USB_SLOT_COUNT ||
        !slots[localSlot].mounted ||
        slots[localSlot].globalSlot == UNIVERSAL_INPUT_SLOT_INVALID
    ) {
        return;
    }

    XInputTransportSlot& slot = slots[localSlot];

    UniversalRumbleSnapshot feedback {};
    if (!UFEEDBACK.snapshot(slot.globalSlot, feedback)) {
        return;
    }

    if (feedback.generation == slot.feedbackGeneration) {
        return;
    }

    // Do not collide with the Xbox One startup exchange.
    if (
        slot.device.protocol == UniversalProtocol::XGIP_XBOX_ONE &&
        (
            slot.xgipTxPending ||
            slot.xgipPhase != XgipInitPhase::READY
        )
    ) {
        return;
    }

    bool sent = false;

    if (slot.device.protocol == UniversalProtocol::XUSB_XBOX360) {
        const uint8_t rumble[8] = {
            0x00, 0x08, 0x00,
            feedback.strong,
            feedback.weak,
            0x00, 0x00, 0x00
        };

        sent = tuh_xinput_send_report(
            slot.devAddr,
            slot.instance,
            rumble,
            sizeof(rumble)
        );
    } else if (
        slot.device.protocol == UniversalProtocol::XGIP_XBOX_ONE
    ) {
        // Xbox One / Series wired GIP rumble.
        // Trigger motors are left at zero here; the two XInput motors
        // are translated to the main left/right motors.
        uint8_t rumble[13] = {
            0x09, 0x00,
            slot.xgipRumbleSequence,
            0x09,
            0x00,
            0x0F,
            0x00,
            0x00,
            feedback.strong,
            feedback.weak,
            0xFF,
            0x00,
            0x00
        };

        sent = tuh_xinput_send_report(
            slot.devAddr,
            slot.instance,
            rumble,
            sizeof(rumble)
        );

        if (sent) {
            slot.xgipRumbleSequence++;
            if (slot.xgipRumbleSequence == 0) {
                slot.xgipRumbleSequence = 1;
            }
        }
    }

    if (sent) {
        slot.feedbackGeneration = feedback.generation;
    }
}

void UniversalXInputHostAddon::restartXgipInit(uint8_t localSlot) {
    if (
        localSlot >= USB_SLOT_COUNT ||
        !slots[localSlot].mounted ||
        slots[localSlot].device.protocol != UniversalProtocol::XGIP_XBOX_ONE
    ) {
        return;
    }

    slots[localSlot].xgipPhase = XgipInitPhase::POWER;
    slots[localSlot].xgipTxPending = false;
}

void UniversalXInputHostAddon::serviceXgipInit(uint8_t localSlot) {
    if (
        localSlot >= USB_SLOT_COUNT ||
        !slots[localSlot].mounted
    ) {
        return;
    }

    XInputTransportSlot& slot = slots[localSlot];

    if (
        slot.device.protocol != UniversalProtocol::XGIP_XBOX_ONE ||
        slot.xgipPhase == XgipInitPhase::NONE ||
        slot.xgipPhase == XgipInitPhase::READY ||
        slot.xgipTxPending
    ) {
        return;
    }

    uint8_t const* packet = nullptr;
    uint16_t packetLen = 0;

    switch (slot.xgipPhase) {
        case XgipInitPhase::POWER:
            packet = XONE_POWER_ON;
            packetLen = sizeof(XONE_POWER_ON);
            break;

        case XgipInitPhase::SYSTEM_INIT:
            packet = XONE_S_INIT;
            packetLen = sizeof(XONE_S_INIT);
            break;

        case XgipInitPhase::EXTRA_INPUT:
            packet = XONE_EXTRA_INPUT;
            packetLen = sizeof(XONE_EXTRA_INPUT);
            break;

        case XgipInitPhase::LED:
            packet = XONE_LED_ON;
            packetLen = sizeof(XONE_LED_ON);
            break;

        case XgipInitPhase::AUTH_DONE:
            packet = XONE_AUTH_DONE;
            packetLen = sizeof(XONE_AUTH_DONE);
            break;

        default:
            return;
    }

    if (
        tuh_xinput_send_report(
            slot.devAddr,
            slot.instance,
            packet,
            packetLen
        )
    ) {
        slot.xgipTxPending = true;
    }
}

void UniversalXInputHostAddon::advanceXgipInit(uint8_t localSlot) {
    if (
        localSlot >= USB_SLOT_COUNT ||
        !slots[localSlot].mounted
    ) {
        return;
    }

    XInputTransportSlot& slot = slots[localSlot];

    switch (slot.xgipPhase) {
        case XgipInitPhase::POWER:
            // Xbox One S and Elite Series 2 need the additional POWER
            // initialization command after ordinary power-on. Other XGIP
            // controllers can continue directly to the common LED/auth path.
            if (
                slot.device.vid == 0x045E &&
                (
                    slot.device.pid == 0x02EA ||
                    slot.device.pid == 0x0B00
                )
            ) {
                slot.xgipPhase = XgipInitPhase::SYSTEM_INIT;
            } else {
                slot.xgipPhase = XgipInitPhase::LED;
            }
            break;

        case XgipInitPhase::SYSTEM_INIT:
            if (slot.device.vid == 0x045E && slot.device.pid == 0x0B00) {
                slot.xgipPhase = XgipInitPhase::EXTRA_INPUT;
            } else {
                slot.xgipPhase = XgipInitPhase::LED;
            }
            break;

        case XgipInitPhase::EXTRA_INPUT:
            slot.xgipPhase = XgipInitPhase::LED;
            break;

        case XgipInitPhase::LED:
            slot.xgipPhase = XgipInitPhase::AUTH_DONE;
            break;

        case XgipInitPhase::AUTH_DONE:
            slot.xgipPhase = XgipInitPhase::READY;
            break;

        default:
            break;
    }
}
