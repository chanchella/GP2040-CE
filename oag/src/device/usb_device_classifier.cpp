#include "oag/device/usb_device_classifier.h"

namespace oag {
namespace {

UsbDeviceClassification match(
    ProtocolKind protocol,
    UsbDriverFamily driver,
    UsbDeviceProfile profile,
    std::uint32_t quirks = UsbQuirkNone
) {
    return {true, protocol, driver, profile, quirks};
}

bool sonyPid(std::uint16_t pid) {
    return pid == 0x0268 || pid == 0x05C4 || pid == 0x09CC ||
           pid == 0x0CE6 || pid == 0x0DF2;
}

UsbDeviceProfile sonyProfile(std::uint16_t pid) {
    if (pid == 0x0268) return UsbDeviceProfile::SonyDualShock3;
    if (pid == 0x05C4 || pid == 0x09CC) return UsbDeviceProfile::SonyDualShock4;
    return UsbDeviceProfile::SonyDualSense;
}

bool isXusbInterface(const UsbDeviceProbe& probe) {
    return
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x5D &&
        (probe.interfaceProtocol == 0x01 ||
         probe.interfaceProtocol == 0x81);
}

bool isXgipInterface(const UsbDeviceProbe& probe) {
    return
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x47 &&
        probe.interfaceProtocol == 0xD0;
}

UsbDeviceProfile razerXboxProfile(std::uint16_t pid) {
    switch (pid) {
        case 0x0A00: return UsbDeviceProfile::RazerAtrox;
        case 0x0A03: return UsbDeviceProfile::RazerWildcat;
        case 0x0A29: return UsbDeviceProfile::RazerWolverineV2;
        case 0x0A3F: return UsbDeviceProfile::RazerWolverineV3Pro;
        case 0x0A43: return UsbDeviceProfile::RazerWolverineV3Tournament;
        default: return UsbDeviceProfile::Unknown;
    }
}

} // namespace

UsbDeviceClassification UsbDeviceClassifier::classify(
    const UsbDeviceProbe& probe
) const {
    if (probe.vid == 0x045E && probe.pid == 0x028E) {
        return match(ProtocolKind::XusbXbox360, UsbDriverFamily::Xinput,
                     UsbDeviceProfile::Xusb045e028e, UsbQuirkXusbStartupOut);
    }

    // Known Razer Xbox-family controllers. Keep the protocol-signature
    // requirement so composite vendor interfaces are never claimed by the
    // XInput host just because the device-level VID/PID matches.
    if (probe.vid == 0x1532 && isXgipInterface(probe)) {
        const UsbDeviceProfile profile = razerXboxProfile(probe.pid);
        if (profile != UsbDeviceProfile::Unknown) {
            return match(
                ProtocolKind::XgipXboxOne,
                UsbDriverFamily::Xinput,
                profile
            );
        }
    }

    // EasySMX/OEM receivers observed in Xbox-compatible PC modes.
    // Prefer a real XUSB interface signature; for these two exact receivers
    // also accept a vendor-specific two-endpoint gameplay interface because
    // some firmware revisions omit the canonical 5D/01 tuple.
    if (
        probe.vid == 0x1A34 &&
        probe.pid == 0xF517 &&
        (
            isXusbInterface(probe) ||
            (probe.interfaceClass == 0xFF && probe.endpointCount >= 2)
        )
    ) {
        return match(
            ProtocolKind::XusbXbox360,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::EasySmxX15Receiver,
            UsbQuirkXusbStartupOut
        );
    }

    if (
        probe.vid == 0x2F24 &&
        probe.pid == 0x0091 &&
        (
            isXusbInterface(probe) ||
            (probe.interfaceClass == 0xFF && probe.endpointCount >= 2)
        )
    ) {
        return match(
            ProtocolKind::XusbXbox360,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::EasySmxLegacy2f240091,
            UsbQuirkXusbStartupOut
        );
    }

    if (probe.vid == 0xCAFE && probe.pid == 0x4012) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     UsbDeviceProfile::AogUniversalHid2,
                     UsbQuirkModernButtonLayout);
    }

    if (probe.vid == 0x054C && sonyPid(probe.pid)) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     sonyProfile(probe.pid), UsbQuirkSonyButtonLayout);
    }

    if (probe.vid == 0x2563 && probe.pid == 0x0575) {
        return match(
            ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
            UsbDeviceProfile::RedragonG808,
            UsbQuirkForceHidGamepad | UsbQuirkZRzAsRightStick |
            UsbQuirkSkipSetIdle
        );
    }

    if (probe.vid == 0x0079 && probe.pid == 0x0006) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     UsbDeviceProfile::GigaMax00790006,
                     UsbQuirkForceHidGamepad | UsbQuirkZRzAsRightStick);
    }

    if (probe.vid == 0x20BC && probe.pid == 0x0055) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     UsbDeviceProfile::Shanwan20bc0055,
                     UsbQuirkForceHidGamepad | UsbQuirkZRzAsRightStick);
    }

    if (probe.vid == 0x20BC && probe.pid == 0x5500) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     UsbDeviceProfile::Shanwan20bc5500,
                     UsbQuirkForceHidGamepad | UsbQuirkZRzAsRightStick);
    }

    if (probe.vid == 0x045E && probe.pid == 0x02EA) {
        return match(ProtocolKind::XgipXboxOne, UsbDriverFamily::Xinput,
                     UsbDeviceProfile::XboxOneS045e02ea);
    }

    if (probe.vid == 0x24C6 && probe.pid == 0x542A) {
        return match(ProtocolKind::XgipXboxOne, UsbDriverFamily::Xinput,
                     UsbDeviceProfile::XboxOneSpectra24c6542a);
    }

    if (probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x5D &&
        probe.interfaceProtocol == 0x01) {
        return match(ProtocolKind::XusbXbox360, UsbDriverFamily::Xinput,
                     UsbDeviceProfile::GenericXusb, UsbQuirkXusbStartupOut);
    }

    if (probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x47 &&
        probe.interfaceProtocol == 0xD0) {
        return match(ProtocolKind::XgipXboxOne, UsbDriverFamily::Xinput,
                     UsbDeviceProfile::GenericXgip);
    }

    if (probe.interfaceClass == 0x03 &&
        probe.interfaceSubClass == 0x01 &&
        probe.interfaceProtocol == 0x01) {
        return match(ProtocolKind::HidKeyboard, UsbDriverFamily::Hid,
                     UsbDeviceProfile::GenericHidKeyboard);
    }

    if (probe.interfaceClass == 0x03 &&
        probe.interfaceSubClass == 0x01 &&
        probe.interfaceProtocol == 0x02) {
        return match(ProtocolKind::HidMouse, UsbDriverFamily::Hid,
                     UsbDeviceProfile::GenericHidMouse);
    }

    if (probe.interfaceClass == 0x03) {
        return match(ProtocolKind::HidGamepad, UsbDriverFamily::Hid,
                     UsbDeviceProfile::GenericHidGamepad);
    }

    return {};
}

} // namespace oag
