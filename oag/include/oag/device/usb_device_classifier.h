#pragma once

#include <cstdint>

#include "oag/device/device_registry.h"

namespace oag {

enum class UsbDriverFamily : std::uint8_t {
    Unknown = 0,
    Hid,
    Xinput,
};

enum class UsbDeviceProfile : std::uint8_t {
    Unknown = 0,
    GenericHidGamepad,
    GenericXusb,
    GenericXgip,
    Xusb045e028e,
    RedragonG808,
    GigaMax00790006,
    Shanwan20bc0055,
    Shanwan20bc5500,
    XboxOneS045e02ea,
    XboxOneSpectra24c6542a,
};

enum UsbInputQuirk : std::uint32_t {
    UsbQuirkNone = 0,
    UsbQuirkForceHidGamepad = 1u << 0,
    UsbQuirkZRzAsRightStick = 1u << 1,
    UsbQuirkXusbStartupOut = 1u << 2,
    UsbQuirkSkipSetIdle = 1u << 3,
};

struct UsbDeviceProbe {
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    std::uint8_t interfaceClass = 0;
    std::uint8_t interfaceSubClass = 0;
    std::uint8_t interfaceProtocol = 0;
    std::uint8_t endpointCount = 0;
};

struct UsbDeviceClassification {
    bool recognized = false;
    ProtocolKind protocol = ProtocolKind::Unknown;
    UsbDriverFamily driver = UsbDriverFamily::Unknown;
    UsbDeviceProfile profile = UsbDeviceProfile::Unknown;
    std::uint32_t quirks = UsbQuirkNone;

    constexpr bool hasQuirk(UsbInputQuirk quirk) const {
        return (quirks & static_cast<std::uint32_t>(quirk)) != 0;
    }
};

class UsbDeviceClassifier {
public:
    UsbDeviceClassification classify(
        const UsbDeviceProbe& probe
    ) const;
};

} // namespace oag
