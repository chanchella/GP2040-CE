#ifndef _UNIVERSAL_DEVICE_REGISTRY_H_
#define _UNIVERSAL_DEVICE_REGISTRY_H_

#include <stdint.h>

// Transport is kept separate from protocol. A GameSir/Redragon/etc. can use
// the same protocol over different transports.
enum class UniversalTransport : uint8_t {
    UNKNOWN = 0,
    USB_WIRED,
    USB_2_4GHZ_DONGLE,
    BLUETOOTH_CLASSIC,
    BLUETOOTH_LE,
};

enum class UniversalDeviceClass : uint8_t {
    UNKNOWN = 0,
    GAMEPAD,
    KEYBOARD,
    MOUSE,
    COMPOSITE,
    AUTHENTICATION,
};

enum class UniversalProtocol : uint8_t {
    UNKNOWN = 0,

    // Standard USB HID families
    HID_GENERIC,
    HID_GAMEPAD,
    HID_KEYBOARD,
    HID_MOUSE,

    // Microsoft / Xbox input families
    XUSB_XBOX360,
    XUSB_AUXILIARY,
    XBOX360_WIRELESS_RECEIVER,
    XGIP_XBOX_ONE,
    XID_XBOX_ORIGINAL,

    // Sony input families
    SONY_DS3,
    SONY_DS4,
    SONY_DUALSENSE,

    // Nintendo
    NINTENDO_SWITCH_PRO,

    // Last-resort extension point for devices with non-standard reports.
    VENDOR_SPECIFIC,
};

enum class UniversalDriverFamily : uint8_t {
    NONE = 0,
    HID,
    XINPUT,
    XBOX360_WIRELESS,
    XGIP,
    XID,
    PLAYSTATION,
    SWITCH,
    VENDOR,
};

enum class UniversalDeviceProfileId : uint8_t {
    NONE = 0,

    // Signature-based families
    GENERIC_HID,
    GENERIC_HID_GAMEPAD,
    GENERIC_HID_KEYBOARD,
    GENERIC_HID_MOUSE,
    GENERIC_XUSB_GAMEPAD,
    GENERIC_XUSB_AUX,
    GENERIC_XBOX360_WIRELESS,
    GENERIC_XGIP,
    GENERIC_XID,

    // Known identities already encountered / useful for future drivers
    XUSB_045E_028E_COMPAT,
    REDRAGON_G808_2563_0575,
    REDRAGON_G808_RAW_24C6_542A,
    SHANWAN_FALLBACK_20BC_0055,
    SHANWAN_FALLBACK_20BC_5500,
    GIGAMAX_0079_0006,
    XBOX_ONE_S_045E_02EA,
    SONY_DS3_054C_0268,
    SONY_DS4_054C_05C4,
    SONY_DS4_054C_09CC,
    SONY_DUALSENSE_054C_0CE6,
    NINTENDO_SWITCH_PRO_057E_2009,
};

enum UniversalDeviceQuirk : uint32_t {
    UNIVERSAL_QUIRK_NONE = 0,

    // Some XUSB-compatible clones vary byte 0 while preserving byte 1=0x14.
    UNIVERSAL_QUIRK_XUSB_STATUS_BYTE_VARIANT = 1u << 0,

    // Some inexpensive XUSB devices remain quiet until ordinary OUT traffic.
    UNIVERSAL_QUIRK_XUSB_STARTUP_OUT = 1u << 1,

    // Known HID devices that misbehave when SET_IDLE is issued.
    UNIVERSAL_QUIRK_SKIP_SET_IDLE = 1u << 2,

    // Reserved for devices that need a vendor-specific initialization exchange.
    UNIVERSAL_QUIRK_VENDOR_INIT = 1u << 3,
};

struct UniversalUsbProbe {
    uint16_t vid = 0;
    uint16_t pid = 0;

    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    uint8_t endpointCount = 0;

    // Optional HID top-level collection information.
    // Generic Desktop = 0x01; Joystick = 0x04; Game Pad = 0x05.
    uint16_t hidUsagePage = 0;
    uint16_t hidUsage = 0;
};

struct UniversalDeviceMatch {
    bool recognized = false;

    UniversalTransport transport = UniversalTransport::UNKNOWN;
    UniversalDeviceClass deviceClass = UniversalDeviceClass::UNKNOWN;
    UniversalProtocol protocol = UniversalProtocol::UNKNOWN;
    UniversalDriverFamily driverFamily = UniversalDriverFamily::NONE;
    UniversalDeviceProfileId profile = UniversalDeviceProfileId::NONE;

    uint32_t quirks = UNIVERSAL_QUIRK_NONE;

    uint16_t vid = 0;
    uint16_t pid = 0;
};

class UniversalDeviceRegistry {
public:
    UniversalDeviceRegistry(UniversalDeviceRegistry const&) = delete;
    void operator=(UniversalDeviceRegistry const&) = delete;

    static UniversalDeviceRegistry& getInstance();

    UniversalDeviceMatch classifyUsb(UniversalUsbProbe const& probe) const;

    static bool hasQuirk(
        UniversalDeviceMatch const& match,
        UniversalDeviceQuirk quirk
    ) {
        return (match.quirks & static_cast<uint32_t>(quirk)) != 0;
    }

    static const char* protocolName(UniversalProtocol protocol);
    static const char* profileName(UniversalDeviceProfileId profile);

private:
    UniversalDeviceRegistry() = default;

    UniversalDeviceMatch classifyKnownUsb(
        UniversalUsbProbe const& probe
    ) const;

    UniversalDeviceMatch classifyUsbSignature(
        UniversalUsbProbe const& probe
    ) const;
};

#define UDEVREG UniversalDeviceRegistry::getInstance()

#endif
