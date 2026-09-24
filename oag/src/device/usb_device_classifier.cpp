#include "oag/device/usb_device_classifier.h"

namespace oag {
namespace {

UsbDeviceClassification match(
    ProtocolKind protocol,
    UsbDriverFamily driver,
    UsbDeviceProfile profile,
    std::uint32_t quirks = UsbQuirkNone
) {
    return {
        true,
        protocol,
        driver,
        profile,
        quirks,
    };
}

} // namespace

UsbDeviceClassification UsbDeviceClassifier::classify(
    const UsbDeviceProbe& probe
) const {
    // Hardware-proven T29/XUSB-compatible identity.
    if (probe.vid == 0x045E && probe.pid == 0x028E) {
        return match(
            ProtocolKind::XusbXbox360,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::Xusb045e028e,
            UsbQuirkXusbStartupOut
        );
    }

    // Redragon G808 receiver. The Golden baseline established that SET_IDLE
    // must be skipped for this receiver and its descriptor can be treated as
    // a gamepad even when collection metadata is imperfect.
    if (probe.vid == 0x2563 && probe.pid == 0x0575) {
        return match(
            ProtocolKind::HidGamepad,
            UsbDriverFamily::Hid,
            UsbDeviceProfile::RedragonG808,
            UsbQuirkForceHidGamepad |
            UsbQuirkZRzAsRightStick |
            UsbQuirkSkipSetIdle
        );
    }

    if (probe.vid == 0x0079 && probe.pid == 0x0006) {
        return match(
            ProtocolKind::HidGamepad,
            UsbDriverFamily::Hid,
            UsbDeviceProfile::GigaMax00790006,
            UsbQuirkForceHidGamepad |
            UsbQuirkZRzAsRightStick
        );
    }

    if (probe.vid == 0x20BC && probe.pid == 0x0055) {
        return match(
            ProtocolKind::HidGamepad,
            UsbDriverFamily::Hid,
            UsbDeviceProfile::Shanwan20bc0055,
            UsbQuirkForceHidGamepad |
            UsbQuirkZRzAsRightStick
        );
    }

    if (probe.vid == 0x20BC && probe.pid == 0x5500) {
        return match(
            ProtocolKind::HidGamepad,
            UsbDriverFamily::Hid,
            UsbDeviceProfile::Shanwan20bc5500,
            UsbQuirkForceHidGamepad |
            UsbQuirkZRzAsRightStick
        );
    }

    // Known Xbox One-family identities.
    if (probe.vid == 0x045E && probe.pid == 0x02EA) {
        return match(
            ProtocolKind::XgipXboxOne,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::XboxOneS045e02ea
        );
    }

    if (probe.vid == 0x24C6 && probe.pid == 0x542A) {
        return match(
            ProtocolKind::XgipXboxOne,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::XboxOneSpectra24c6542a
        );
    }

    // Protocol signatures are stronger than product IDs and cover unknown
    // compatible controllers.
    if (
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x5D &&
        probe.interfaceProtocol == 0x01
    ) {
        return match(
            ProtocolKind::XusbXbox360,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::GenericXusb,
            UsbQuirkXusbStartupOut
        );
    }

    if (
        probe.interfaceClass == 0xFF &&
        probe.interfaceSubClass == 0x47 &&
        probe.interfaceProtocol == 0xD0
    ) {
        return match(
            ProtocolKind::XgipXboxOne,
            UsbDriverFamily::Xinput,
            UsbDeviceProfile::GenericXgip
        );
    }

    if (probe.interfaceClass == 0x03) {
        return match(
            ProtocolKind::HidGamepad,
            UsbDriverFamily::Hid,
            UsbDeviceProfile::GenericHidGamepad
        );
    }

    return {};
}

} // namespace oag
