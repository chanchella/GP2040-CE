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
constexpr std::uint16_t kDevicePid = 0x4012;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcHidOutput::kOutputSlots;
constexpr std::uint8_t kEndpointPacketSize = 16;
constexpr std::uint8_t kPollingIntervalMs = 1;

// AOG universal canonical USB gamepad persona.
//
// The first ten buttons follow the documented Microsoft XUSB-to-HID order.
// Cross-family semantic normalization happens before this output layer:
// A/Cross, B/Circle, X/Square, Y/Triangle, LB/L1, RB/R1,
// View/Select, Menu/Options, L3, R3. Guide/Home and Share/Create/Capture
// extend that stable order as buttons 11 and 12.
//
// Keep the PC-HID1 hardware-verified shape unchanged:
// 16 buttons + one eight-way Hat Switch + six analog axes.
//   X/Y     = left stick
//   Rx/Ry   = right stick
//   Z/Rz    = LT/L2 and RT/R2, independently
//
// This remains standard USB HID. Native Xbox/PlayStation console personas
// require their dedicated platform protocol/authentication path and are not
// impersonated by this generic HID descriptor.
//
// Report bytes:
//   0..1 buttons
//   2    hat (low nibble; 8 = neutral)
//   3..6 X/Y/Rx/Ry signed sticks
//   7..8 Z/Rz unsigned triggers
const std::uint8_t kHidReportDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)

    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (Button 1)
    0x29, 0x10,       //   Usage Maximum (Button 16)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x10,       //   Report Count (16)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Usage (Hat Switch)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (English Rotation, Degrees)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data, Variable, Absolute, Null State)
    0x65, 0x00,       //   Unit (None)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x03,       //   Input (Constant, Variable, Absolute)

    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x33,       //   Usage (Rx)
    0x09, 0x34,       //   Usage (Ry)
    0x15, 0x81,       //   Logical Minimum (-127)
    0x25, 0x7F,       //   Logical Maximum (127)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    0x09, 0x32,       //   Usage (Z)
    0x09, 0x35,       //   Usage (Rz)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    0xC0,             // End Collection
};

const std::uint8_t kDeviceDescriptor[] = {
    0x12, 0x01,             // bLength, bDescriptorType
    0x00, 0x02,             // bcdUSB 2.00
    0x00, 0x00, 0x00,       // class/subclass/protocol per interface
    0x40,                   // EP0 = 64 bytes
    static_cast<std::uint8_t>(kDeviceVid & 0xFFu),
    static_cast<std::uint8_t>(kDeviceVid >> 8),
    static_cast<std::uint8_t>(kDevicePid & 0xFFu),
    static_cast<std::uint8_t>(kDevicePid >> 8),
    0x02, 0x01,             // bcdDevice 1.02
    0x01, 0x02, 0x03,       // manufacturer/product/serial
    0x01,                   // one configuration
};

constexpr std::uint16_t kConfigurationLength =
    9u + static_cast<std::uint16_t>(kOutputSlots) * (9u + 9u + 7u);

const std::uint8_t kConfigurationDescriptor[] = {
    // Configuration: 4 independent HID gamepad interfaces.
    0x09, 0x02,
    static_cast<std::uint8_t>(kConfigurationLength & 0xFFu),
    static_cast<std::uint8_t>(kConfigurationLength >> 8),
    0x04,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0x80,       // bus powered
    0x32,       // 100 mA

    // Gamepad 1 — interface 0 — EP 81
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) & 0xFFu),
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) >> 8),
    0x07, 0x05, 0x81, 0x03, kEndpointPacketSize, 0x00,
    kPollingIntervalMs,

    // Gamepad 2 — interface 1 — EP 82
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) & 0xFFu),
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) >> 8),
    0x07, 0x05, 0x82, 0x03, kEndpointPacketSize, 0x00,
    kPollingIntervalMs,

    // Gamepad 3 — interface 2 — EP 83
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) & 0xFFu),
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) >> 8),
    0x07, 0x05, 0x83, 0x03, kEndpointPacketSize, 0x00,
    kPollingIntervalMs,

    // Gamepad 4 — interface 3 — EP 84
    0x09, 0x04, 0x03, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) & 0xFFu),
    static_cast<std::uint8_t>(sizeof(kHidReportDescriptor) >> 8),
    0x07, 0x05, 0x84, 0x03, kEndpointPacketSize, 0x00,
    kPollingIntervalMs,
};

static_assert(sizeof(kDeviceDescriptor) == 18);
static_assert(kOutputSlots == 4);
static_assert(kConfigurationLength == 109);
static_assert(sizeof(kConfigurationDescriptor) == kConfigurationLength);

std::uint16_t gStringDescriptor[64] {};
char gSerial[32] {};

const char* stringValue(std::uint8_t index) {
    switch (index) {
        case 1:
            return oag::product::kManufacturer;
        case 2:
            return "AOG Abo Gemi ultra gaming";
        case 3: {
            pico_unique_board_id_t id {};
            pico_get_unique_board_id(&id);

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "AOG-HID2-%02X%02X%02X%02X%02X%02X",
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
    if (instance >= kOutputSlots) {
        return nullptr;
    }

    return kHidReportDescriptor;
}

extern "C" std::uint16_t const* tud_descriptor_string_cb(
    std::uint8_t index,
    std::uint16_t langid
) {
    (void)langid;

    if (index == 0) {
        gStringDescriptor[1] = 0x0409;
        gStringDescriptor[0] =
            static_cast<std::uint16_t>((TUSB_DESC_STRING << 8) | 4);
        return gStringDescriptor;
    }

    const char* text = stringValue(index);
    if (text == nullptr) {
        return nullptr;
    }

    const std::size_t length =
        std::min<std::size_t>(std::strlen(text), 63);

    for (std::size_t i = 0; i < length; ++i) {
        gStringDescriptor[i + 1] =
            static_cast<std::uint8_t>(text[i]);
    }

    gStringDescriptor[0] = static_cast<std::uint16_t>(
        (TUSB_DESC_STRING << 8) |
        (2u * length + 2u)
    );

    return gStringDescriptor;
}
