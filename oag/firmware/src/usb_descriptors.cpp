#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/unique_id.h"
#include "tusb.h"

#include "oag/core/product_identity.h"
#include "oag/firmware/pc_hid_output.h"

namespace {

constexpr std::uint16_t kDeviceVid = 0xCAFE;
constexpr std::uint16_t kDevicePid = 0x4017;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcHidOutput::kOutputSlots;

constexpr std::uint8_t kGamepadEndpointSize = 16;
constexpr std::uint8_t kKmEndpointSize = 8;
constexpr std::uint8_t kPollingIntervalMs = 1;

const std::uint8_t kGamepadReportDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)

    // First four axes intentionally match Web Gamepad axes[0..3]:
    // left X/Y then right X/Y.
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x32,       //   Usage (Z)
    0x09, 0x35,       //   Usage (Rz)
    0x15, 0x81,       //   Logical Minimum (-127)
    0x25, 0x7F,       //   Logical Maximum (127)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    // Android/Linux-friendly analog trigger usages.
    // Brake = left trigger, Accelerator = right trigger.
    0x05, 0x02,       //   Usage Page (Simulation Controls)
    0x09, 0xC5,       //   Usage (Brake)
    0x09, 0xC4,       //   Usage (Accelerator)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    // Keep a native HID hat for Android/native games.
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Usage (Hat Switch)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (English Rotation, Degrees)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data, Variable, Absolute, Null State)
    0x65, 0x00,       //   Unit (None)

    // 18 buttons deliberately mirror Web Gamepad standard button indices.
    // D-pad and analog triggers are duplicated as buttons so browsers that
    // expose only raw HID arrays still receive every gameplay control.
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (Button 1)
    0x29, 0x12,       //   Usage Maximum (Button 18)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x12,       //   Report Count (18)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    // Pad button bits to the next byte boundary.
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x06,       //   Report Count (6)
    0x81, 0x03,       //   Input (Constant, Variable, Absolute)

    0xC0,             // End Collection
};

const std::uint8_t kKeyboardReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const std::uint8_t kMouseReportDescriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

const std::uint8_t kDeviceDescriptor[] = {
    0x12, 0x01,             // bLength, bDescriptorType
    0x00, 0x02,             // USB 2.00
    0x00, 0x00, 0x00,       // class/subclass/protocol per interface
    0x40,                   // EP0 = 64
    static_cast<std::uint8_t>(kDeviceVid & 0xFFu),
    static_cast<std::uint8_t>(kDeviceVid >> 8),
    static_cast<std::uint8_t>(kDevicePid & 0xFFu),
    static_cast<std::uint8_t>(kDevicePid >> 8),
    0x00, 0x01,             // bcdDevice 1.00
    0x01, 0x02, 0x03,       // manufacturer/product/serial
    0x01,                   // one configuration
};

constexpr std::uint8_t kInterfaceCount = 6;
constexpr std::uint16_t kConfigurationLength =
    TUD_CONFIG_DESC_LEN +
    static_cast<std::uint16_t>(kInterfaceCount) * TUD_HID_DESC_LEN;

const std::uint8_t kConfigurationDescriptor[] = {
    // Configuration: four independent standard HID gamepads followed by the
    // Golden native keyboard and mouse interfaces.
    TUD_CONFIG_DESCRIPTOR(
        1,
        kInterfaceCount,
        0,
        kConfigurationLength,
        0x00,
        100
    ),

    // Gamepad 1 — HID instance 0 — interface 0 — EP 81
    TUD_HID_DESCRIPTOR(
        0,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kGamepadReportDescriptor),
        0x81,
        kGamepadEndpointSize,
        kPollingIntervalMs
    ),

    // Gamepad 2 — HID instance 1 — interface 1 — EP 82
    TUD_HID_DESCRIPTOR(
        1,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kGamepadReportDescriptor),
        0x82,
        kGamepadEndpointSize,
        kPollingIntervalMs
    ),

    // Gamepad 3 — HID instance 2 — interface 2 — EP 83
    TUD_HID_DESCRIPTOR(
        2,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kGamepadReportDescriptor),
        0x83,
        kGamepadEndpointSize,
        kPollingIntervalMs
    ),

    // Gamepad 4 — HID instance 3 — interface 3 — EP 84
    TUD_HID_DESCRIPTOR(
        3,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kGamepadReportDescriptor),
        0x84,
        kGamepadEndpointSize,
        kPollingIntervalMs
    ),

    // Native keyboard — HID instance 4 — interface 4 — EP 85
    TUD_HID_DESCRIPTOR(
        4,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(kKeyboardReportDescriptor),
        0x85,
        kKmEndpointSize,
        kPollingIntervalMs
    ),

    // Native mouse — HID instance 5 — interface 5 — EP 86
    TUD_HID_DESCRIPTOR(
        5,
        0,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(kMouseReportDescriptor),
        0x86,
        kKmEndpointSize,
        kPollingIntervalMs
    ),
};

static_assert(sizeof(kDeviceDescriptor) == 18);
static_assert(kOutputSlots == 4);
static_assert(kInterfaceCount == 6);
static_assert(sizeof(kConfigurationDescriptor) == kConfigurationLength);

std::uint16_t gStringDescriptor[64] {};
char gSerial[32] {};

const char* stringValue(std::uint8_t index) {
    switch (index) {
        case 1:
            return oag::product::kManufacturer;
        case 2:
            return "OAG Mobile USB Gamepad";
        case 3: {
            pico_unique_board_id_t id {};
            pico_get_unique_board_id(&id);

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "OAG-MOB-%02X%02X%02X%02X%02X%02X",
                id.id[2],
                id.id[3],
                id.id[4],
                id.id[5],
                id.id[6],
                id.id[7]
            );
            return gSerial;
        }
        default:
            return nullptr;
    }
}

} // namespace

extern "C" std::uint8_t const* tud_descriptor_device_cb(void) {
    return kDeviceDescriptor;
}

extern "C" std::uint8_t const* tud_descriptor_configuration_cb(
    std::uint8_t index
) {
    (void)index;
    return kConfigurationDescriptor;
}

extern "C" std::uint8_t const* tud_hid_descriptor_report_cb(
    std::uint8_t instance
) {
    if (instance < kOutputSlots) {
        return kGamepadReportDescriptor;
    }

    switch (instance) {
        case 4:
            return kKeyboardReportDescriptor;
        case 5:
            return kMouseReportDescriptor;
        default:
            return nullptr;
    }
}

extern "C" std::uint16_t const* tud_descriptor_string_cb(
    std::uint8_t index,
    std::uint16_t langid
) {
    (void)langid;

    if (index == 0) {
        gStringDescriptor[1] = 0x0409;
        gStringDescriptor[0] =
            static_cast<std::uint16_t>(
                (TUSB_DESC_STRING << 8) | 4
            );
        return gStringDescriptor;
    }

    const char* text = stringValue(index);
    if (text == nullptr) {
        return nullptr;
    }

    const std::size_t length =
        std::min<std::size_t>(
            std::strlen(text),
            63
        );

    for (std::size_t i = 0; i < length; ++i) {
        gStringDescriptor[i + 1] =
            static_cast<std::uint8_t>(text[i]);
    }

    gStringDescriptor[0] =
        static_cast<std::uint16_t>(
            (TUSB_DESC_STRING << 8) |
            (2u * length + 2u)
        );

    return gStringDescriptor;
}
