#include "input/universal_device_registry.h"

namespace {

UniversalDeviceMatch makeMatch(
    UniversalUsbProbe const& probe,
    UniversalTransport transport,
    UniversalDeviceClass deviceClass,
    UniversalProtocol protocol,
    UniversalDriverFamily driverFamily,
    UniversalDeviceProfileId profile,
    uint32_t quirks = UNIVERSAL_QUIRK_NONE
) {
    UniversalDeviceMatch match {};
    match.recognized = true;
    match.transport = transport;
    match.deviceClass = deviceClass;
    match.protocol = protocol;
    match.driverFamily = driverFamily;
    match.profile = profile;
    match.quirks = quirks;
    match.vid = probe.vid;
    match.pid = probe.pid;
    return match;
}

} // namespace

UniversalDeviceRegistry& UniversalDeviceRegistry::getInstance() {
    static UniversalDeviceRegistry instance;
    return instance;
}

UniversalDeviceMatch UniversalDeviceRegistry::classifyUsb(
    UniversalUsbProbe const& probe
) const {
    UniversalDeviceMatch known = classifyKnownUsb(probe);
    if (known.recognized) {
        return known;
    }

    UniversalDeviceMatch signature = classifyUsbSignature(probe);
    if (signature.recognized) {
        return signature;
    }

    UniversalDeviceMatch unknown {};
    unknown.vid = probe.vid;
    unknown.pid = probe.pid;
    return unknown;
}

UniversalDeviceMatch UniversalDeviceRegistry::classifyKnownUsb(
    UniversalUsbProbe const& probe
) const {
    // 045E:028E is the identity observed from the proven T29 path.
    // Many Xbox 360-compatible devices/clones use the same identity,
    // therefore the profile name intentionally describes compatibility
    // rather than claiming a specific manufacturer/model.
    if (probe.vid == 0x045E && probe.pid == 0x028E) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::XUSB_XBOX360,
            UniversalDriverFamily::XINPUT,
            UniversalDeviceProfileId::XUSB_045E_028E_COMPAT,
            UNIVERSAL_QUIRK_XUSB_STATUS_BYTE_VARIANT |
            UNIVERSAL_QUIRK_XUSB_STARTUP_OUT
        );
    }

    // Redragon Harrow G808 identities found in public hardware reports.
    // 2563:0575 has been reported for the G808 USB receiver.
    if (probe.vid == 0x2563 && probe.pid == 0x0575) {
        return makeMatch(
            probe,
            UniversalTransport::USB_2_4GHZ_DONGLE,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::HID_GAMEPAD,
            UniversalDriverFamily::HID,
            UniversalDeviceProfileId::REDRAGON_G808_2563_0575
        );
    }

    // Some G808 receivers have also been observed in a pre-XInput/raw stage
    // as 24C6:542A. If that stage is HID, the generic HID parser can consume
    // it. Otherwise keep the identity visible as vendor-specific for a later
    // mode-switch driver rather than pretending it is already XInput.
    if (probe.vid == 0x24C6 && probe.pid == 0x542A) {
        const bool hidInterface = probe.interfaceClass == 0x03;

        return makeMatch(
            probe,
            UniversalTransport::USB_2_4GHZ_DONGLE,
            UniversalDeviceClass::GAMEPAD,
            hidInterface
                ? UniversalProtocol::HID_GAMEPAD
                : UniversalProtocol::VENDOR_SPECIFIC,
            hidInterface
                ? UniversalDriverFamily::HID
                : UniversalDriverFamily::VENDOR,
            UniversalDeviceProfileId::REDRAGON_G808_RAW_24C6_542A
        );
    }

    // Shanwan-compatible receivers may deliberately re-enumerate into
    // fallback HID identities after an unsupported host-side control request.
    if (probe.vid == 0x20BC && probe.pid == 0x0055) {
        return makeMatch(
            probe,
            UniversalTransport::USB_2_4GHZ_DONGLE,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::HID_GAMEPAD,
            UniversalDriverFamily::HID,
            UniversalDeviceProfileId::SHANWAN_FALLBACK_20BC_0055
        );
    }

    if (probe.vid == 0x20BC && probe.pid == 0x5500) {
        return makeMatch(
            probe,
            UniversalTransport::USB_2_4GHZ_DONGLE,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::HID_GAMEPAD,
            UniversalDriverFamily::HID,
            UniversalDeviceProfileId::SHANWAN_FALLBACK_20BC_5500
        );
    }

    // A commonly reported GIGAMAX / Speedlink-compatible USB identity.
    // Other GIGAMAX models remain covered by descriptor-driven Generic HID.
    if (probe.vid == 0x0079 && probe.pid == 0x0006) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::HID_GAMEPAD,
            UniversalDriverFamily::HID,
            UniversalDeviceProfileId::GIGAMAX_0079_0006
        );
    }

    // Xbox One S wired controller.
    if (probe.vid == 0x045E && probe.pid == 0x02EA) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::XGIP_XBOX_ONE,
            UniversalDriverFamily::XGIP,
            UniversalDeviceProfileId::XBOX_ONE_S_045E_02EA
        );
    }

    // Sony identities already present in the GP2040-CE descriptor base.
    if (probe.vid == 0x054C && probe.pid == 0x0268) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::SONY_DS3,
            UniversalDriverFamily::PLAYSTATION,
            UniversalDeviceProfileId::SONY_DS3_054C_0268
        );
    }

    if (probe.vid == 0x054C && probe.pid == 0x05C4) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::SONY_DS4,
            UniversalDriverFamily::PLAYSTATION,
            UniversalDeviceProfileId::SONY_DS4_054C_05C4
        );
    }

    if (probe.vid == 0x054C && probe.pid == 0x09CC) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::SONY_DS4,
            UniversalDriverFamily::PLAYSTATION,
            UniversalDeviceProfileId::SONY_DS4_054C_09CC
        );
    }


    if (probe.vid == 0x054C && probe.pid == 0x0CE6) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::SONY_DUALSENSE,
            UniversalDriverFamily::PLAYSTATION,
            UniversalDeviceProfileId::SONY_DUALSENSE_054C_0CE6
        );
    }

    if (probe.vid == 0x057E && probe.pid == 0x2009) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::NINTENDO_SWITCH_PRO,
            UniversalDriverFamily::SWITCH,
            UniversalDeviceProfileId::NINTENDO_SWITCH_PRO_057E_2009
        );
    }

    return UniversalDeviceMatch {};
}

UniversalDeviceMatch UniversalDeviceRegistry::classifyUsbSignature(
    UniversalUsbProbe const& probe
) const {
    // Xbox 360 / XUSB family.
    if (
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x5D
    ) {
        if (probe.interfaceProtocol == 0x01) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::GAMEPAD,
                UniversalProtocol::XUSB_XBOX360,
                UniversalDriverFamily::XINPUT,
                UniversalDeviceProfileId::GENERIC_XUSB_GAMEPAD,
                UNIVERSAL_QUIRK_XUSB_STARTUP_OUT
            );
        }

        if (
            probe.interfaceProtocol == 0x02 ||
            probe.interfaceProtocol == 0x03
        ) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::COMPOSITE,
                UniversalProtocol::XUSB_AUXILIARY,
                UniversalDriverFamily::XINPUT,
                UniversalDeviceProfileId::GENERIC_XUSB_AUX
            );
        }

        if (probe.interfaceProtocol == 0x81) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::COMPOSITE,
                UniversalProtocol::XBOX360_WIRELESS_RECEIVER,
                UniversalDriverFamily::XBOX360_WIRELESS,
                UniversalDeviceProfileId::GENERIC_XBOX360_WIRELESS
            );
        }
    }

    // Xbox One / later USB GIP/XGIP-style interface signature.
    if (
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x47 &&
        probe.interfaceProtocol == 0xD0
    ) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::XGIP_XBOX_ONE,
            UniversalDriverFamily::XGIP,
            UniversalDeviceProfileId::GENERIC_XGIP
        );
    }

    // Original Xbox XID interface signature.
    if (
        probe.interfaceClass == 0x58 &&
        probe.interfaceSubClass == 0x42
    ) {
        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::GAMEPAD,
            UniversalProtocol::XID_XBOX_ORIGINAL,
            UniversalDriverFamily::XID,
            UniversalDeviceProfileId::GENERIC_XID
        );
    }

    // Standard USB HID.
    if (probe.interfaceClass == 0x03) {
        if (probe.interfaceProtocol == 0x01) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::KEYBOARD,
                UniversalProtocol::HID_KEYBOARD,
                UniversalDriverFamily::HID,
                UniversalDeviceProfileId::GENERIC_HID_KEYBOARD
            );
        }

        if (probe.interfaceProtocol == 0x02) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::MOUSE,
                UniversalProtocol::HID_MOUSE,
                UniversalDriverFamily::HID,
                UniversalDeviceProfileId::GENERIC_HID_MOUSE
            );
        }

        const bool genericDesktop =
            probe.hidUsagePage == 0x01;

        const bool gamepadOrJoystick =
            probe.hidUsage == 0x04 ||
            probe.hidUsage == 0x05 ||
            probe.hidUsage == 0x08;

        if (genericDesktop && gamepadOrJoystick) {
            return makeMatch(
                probe,
                UniversalTransport::USB_WIRED,
                UniversalDeviceClass::GAMEPAD,
                UniversalProtocol::HID_GAMEPAD,
                UniversalDriverFamily::HID,
                UniversalDeviceProfileId::GENERIC_HID_GAMEPAD
            );
        }

        return makeMatch(
            probe,
            UniversalTransport::USB_WIRED,
            UniversalDeviceClass::COMPOSITE,
            UniversalProtocol::HID_GENERIC,
            UniversalDriverFamily::HID,
            UniversalDeviceProfileId::GENERIC_HID
        );
    }

    return UniversalDeviceMatch {};
}

const char* UniversalDeviceRegistry::protocolName(
    UniversalProtocol protocol
) {
    switch (protocol) {
        case UniversalProtocol::HID_GENERIC: return "HID_GENERIC";
        case UniversalProtocol::HID_GAMEPAD: return "HID_GAMEPAD";
        case UniversalProtocol::HID_KEYBOARD: return "HID_KEYBOARD";
        case UniversalProtocol::HID_MOUSE: return "HID_MOUSE";
        case UniversalProtocol::XUSB_XBOX360: return "XUSB_XBOX360";
        case UniversalProtocol::XUSB_AUXILIARY: return "XUSB_AUXILIARY";
        case UniversalProtocol::XBOX360_WIRELESS_RECEIVER: return "XBOX360_WIRELESS_RECEIVER";
        case UniversalProtocol::XGIP_XBOX_ONE: return "XGIP_XBOX_ONE";
        case UniversalProtocol::XID_XBOX_ORIGINAL: return "XID_XBOX_ORIGINAL";
        case UniversalProtocol::SONY_DS3: return "SONY_DS3";
        case UniversalProtocol::SONY_DS4: return "SONY_DS4";
        case UniversalProtocol::SONY_DUALSENSE: return "SONY_DUALSENSE";
        case UniversalProtocol::NINTENDO_SWITCH_PRO: return "NINTENDO_SWITCH_PRO";
        case UniversalProtocol::VENDOR_SPECIFIC: return "VENDOR_SPECIFIC";
        case UniversalProtocol::UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

const char* UniversalDeviceRegistry::profileName(
    UniversalDeviceProfileId profile
) {
    switch (profile) {
        case UniversalDeviceProfileId::GENERIC_HID: return "GENERIC_HID";
        case UniversalDeviceProfileId::GENERIC_HID_GAMEPAD: return "GENERIC_HID_GAMEPAD";
        case UniversalDeviceProfileId::GENERIC_HID_KEYBOARD: return "GENERIC_HID_KEYBOARD";
        case UniversalDeviceProfileId::GENERIC_HID_MOUSE: return "GENERIC_HID_MOUSE";
        case UniversalDeviceProfileId::GENERIC_XUSB_GAMEPAD: return "GENERIC_XUSB_GAMEPAD";
        case UniversalDeviceProfileId::GENERIC_XUSB_AUX: return "GENERIC_XUSB_AUX";
        case UniversalDeviceProfileId::GENERIC_XBOX360_WIRELESS: return "GENERIC_XBOX360_WIRELESS";
        case UniversalDeviceProfileId::GENERIC_XGIP: return "GENERIC_XGIP";
        case UniversalDeviceProfileId::GENERIC_XID: return "GENERIC_XID";
        case UniversalDeviceProfileId::XUSB_045E_028E_COMPAT: return "XUSB_045E_028E_COMPAT";
        case UniversalDeviceProfileId::REDRAGON_G808_2563_0575: return "REDRAGON_G808_2563_0575";
        case UniversalDeviceProfileId::REDRAGON_G808_RAW_24C6_542A: return "REDRAGON_G808_RAW_24C6_542A";
        case UniversalDeviceProfileId::SHANWAN_FALLBACK_20BC_0055: return "SHANWAN_FALLBACK_20BC_0055";
        case UniversalDeviceProfileId::SHANWAN_FALLBACK_20BC_5500: return "SHANWAN_FALLBACK_20BC_5500";
        case UniversalDeviceProfileId::GIGAMAX_0079_0006: return "GIGAMAX_0079_0006";
        case UniversalDeviceProfileId::XBOX_ONE_S_045E_02EA: return "XBOX_ONE_S_045E_02EA";
        case UniversalDeviceProfileId::SONY_DS3_054C_0268: return "SONY_DS3_054C_0268";
        case UniversalDeviceProfileId::SONY_DS4_054C_05C4: return "SONY_DS4_054C_05C4";
        case UniversalDeviceProfileId::SONY_DS4_054C_09CC: return "SONY_DS4_054C_09CC";
        case UniversalDeviceProfileId::SONY_DUALSENSE_054C_0CE6: return "SONY_DUALSENSE_054C_0CE6";
        case UniversalDeviceProfileId::NINTENDO_SWITCH_PRO_057E_2009: return "NINTENDO_SWITCH_PRO_057E_2009";
        case UniversalDeviceProfileId::NONE:
        default:
            return "NONE";
    }
}
