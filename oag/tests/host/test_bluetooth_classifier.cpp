#include <cassert>

#include "oag/bluetooth/bluetooth_device_classifier.h"

using namespace oag;

int main() {
    BluetoothDeviceClassifier classifier;

    const auto keyboard = classifier.classify({
        TransportType::BluetoothLe,
        0,
        0,
        0x03C1,
        0,
        0,
    });
    assert(keyboard.recognized);
    assert(keyboard.protocol == ProtocolKind::HidKeyboard);

    const auto mouse = classifier.classify({
        TransportType::BluetoothLe,
        0,
        0,
        0x03C2,
        0,
        0,
    });
    assert(mouse.protocol == ProtocolKind::HidMouse);

    const auto genericPad = classifier.classify({
        TransportType::BluetoothLe,
        0,
        0,
        0x03C4,
        0,
        0,
    });
    assert(genericPad.protocol == ProtocolKind::HidGamepad);
    assert(
        genericPad.profile ==
        BluetoothDeviceProfile::GenericGamepad
    );

    const auto dualsense = classifier.classify({
        TransportType::BluetoothClassic,
        0x054C,
        0x0CE6,
        0,
        0,
        0,
    });
    assert(
        dualsense.profile ==
        BluetoothDeviceProfile::SonyDualSense
    );
    assert(
        (dualsense.capabilities & BluetoothCapTriggerRumble) != 0
    );

    const auto xboxBle = classifier.classify({
        TransportType::BluetoothLe,
        0x045E,
        0,
        0,
        0x01,
        0x05,
    });
    assert(
        xboxBle.profile ==
        BluetoothDeviceProfile::XboxBleGamepad
    );

    const auto switchPro = classifier.classify({
        TransportType::BluetoothClassic,
        0x057E,
        0x2009,
        0,
        0,
        0,
    });
    assert(
        switchPro.profile ==
        BluetoothDeviceProfile::NintendoSwitch
    );

    const auto invalid = classifier.classify({
        TransportType::UsbPioHost,
        0x045E,
        0,
        0x03C4,
        0,
        0,
    });
    assert(!invalid.recognized);

    return 0;
}
