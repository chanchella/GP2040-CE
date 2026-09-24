#include "oag/bluetooth/bluetooth_device_classifier.h"

namespace oag {
namespace {

BluetoothDeviceClassification match(
    ProtocolKind protocol,
    BluetoothDeviceProfile profile,
    std::uint32_t capabilities = BluetoothCapNone
) {
    return {
        true,
        protocol,
        profile,
        capabilities,
    };
}

bool genericDesktopGamepad(
    std::uint16_t page,
    std::uint16_t usage
) {
    return page == 0x01 &&
        (usage == 0x04 || usage == 0x05 || usage == 0x08);
}

} // namespace

BluetoothDeviceClassification BluetoothDeviceClassifier::classify(
    const BluetoothDeviceProbe& probe
) const {
    if (probe.transport != TransportType::BluetoothClassic &&
        probe.transport != TransportType::BluetoothLe) {
        return {};
    }

    // Bluetooth SIG HID appearances:
    // 0x03C0 generic HID, 0x03C1 keyboard, 0x03C2 mouse,
    // 0x03C3 joystick, 0x03C4 gamepad.
    if (probe.appearance == 0x03C1) {
        return match(
            ProtocolKind::HidKeyboard,
            BluetoothDeviceProfile::GenericKeyboard
        );
    }

    if (probe.appearance == 0x03C2) {
        return match(
            ProtocolKind::HidMouse,
            BluetoothDeviceProfile::GenericMouse
        );
    }

    if (probe.appearance == 0x03C3 ||
        probe.appearance == 0x03C4) {
        return match(
            ProtocolKind::HidGamepad,
            BluetoothDeviceProfile::GenericGamepad
        );
    }

    if (probe.vid == 0x054C) {
        if (probe.pid == 0x05C4 || probe.pid == 0x09CC) {
            return match(
                ProtocolKind::HidGamepad,
                BluetoothDeviceProfile::SonyDualShock4,
                BluetoothCapRumble |
                BluetoothCapLed |
                BluetoothCapMotion |
                BluetoothCapTouch
            );
        }

        if (probe.pid == 0x0CE6 || probe.pid == 0x0DF2) {
            return match(
                ProtocolKind::HidGamepad,
                BluetoothDeviceProfile::SonyDualSense,
                BluetoothCapRumble |
                BluetoothCapTriggerRumble |
                BluetoothCapLed |
                BluetoothCapMotion |
                BluetoothCapTouch
            );
        }
    }

    if (probe.vid == 0x057E &&
        (probe.pid == 0x2006 ||
         probe.pid == 0x2007 ||
         probe.pid == 0x2009)) {
        return match(
            ProtocolKind::HidGamepad,
            BluetoothDeviceProfile::NintendoSwitch,
            BluetoothCapRumble |
                BluetoothCapMotion |
                BluetoothCapLed
        );
    }

    if (probe.vid == 0x045E &&
        probe.transport == TransportType::BluetoothLe) {
        return match(
            ProtocolKind::HidGamepad,
            BluetoothDeviceProfile::XboxBleGamepad,
            BluetoothCapRumble |
                BluetoothCapTriggerRumble
        );
    }

    if (genericDesktopGamepad(probe.usagePage, probe.usage)) {
        return match(
            ProtocolKind::HidGamepad,
            BluetoothDeviceProfile::GenericGamepad
        );
    }

    if (probe.usagePage == 0x01 && probe.usage == 0x06) {
        return match(
            ProtocolKind::HidKeyboard,
            BluetoothDeviceProfile::GenericKeyboard
        );
    }

    if (probe.usagePage == 0x01 && probe.usage == 0x02) {
        return match(
            ProtocolKind::HidMouse,
            BluetoothDeviceProfile::GenericMouse
        );
    }

    if (probe.appearance == 0x03C0) {
        return match(
            ProtocolKind::Unknown,
            BluetoothDeviceProfile::GenericHid
        );
    }

    return {};
}

} // namespace oag
