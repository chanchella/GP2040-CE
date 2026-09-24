#pragma once

#include <cstdint>

#include "oag/device/device_registry.h"

namespace oag {

enum class BluetoothDeviceProfile : std::uint8_t {
    Unknown = 0,
    GenericHid,
    GenericGamepad,
    GenericKeyboard,
    GenericMouse,
    XboxBleGamepad,
    SonyDualShock4,
    SonyDualSense,
    NintendoSwitch,
};

enum BluetoothCapability : std::uint32_t {
    BluetoothCapNone = 0,
    BluetoothCapRumble = 1u << 0,
    BluetoothCapTriggerRumble = 1u << 1,
    BluetoothCapLed = 1u << 2,
    BluetoothCapMotion = 1u << 3,
    BluetoothCapTouch = 1u << 4,
};

struct BluetoothDeviceProbe {
    TransportType transport = TransportType::BluetoothLe;
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    std::uint16_t appearance = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
};

struct BluetoothDeviceClassification {
    bool recognized = false;
    ProtocolKind protocol = ProtocolKind::Unknown;
    BluetoothDeviceProfile profile = BluetoothDeviceProfile::Unknown;
    std::uint32_t capabilities = BluetoothCapNone;
};

class BluetoothDeviceClassifier {
public:
    BluetoothDeviceClassification classify(
        const BluetoothDeviceProbe& probe
    ) const;
};

} // namespace oag
