#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "tusb.h"

#include "oag/core/product_identity.h"

namespace {

constexpr std::uint16_t kLegacyDevelopmentVid = 0x10C4;
constexpr std::uint16_t kLegacyDevelopmentPid = 0x82C0;

constexpr std::uint8_t kInterfaceGamepad = 0;
constexpr std::uint8_t kInterfaceCount = 1;
constexpr std::uint8_t kEndpointGamepadIn = 0x81;
constexpr std::uint16_t kEndpointSize = 64;
constexpr std::uint8_t kGamepadReportId = 1;

const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = kLegacyDevelopmentVid,
    .idProduct = kLegacyDevelopmentPid,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

// U1 development HID profile:
// 16 buttons, one hat, four signed 8-bit axes, two 8-bit triggers.
// The legacy VID/PID above is retained only for continuity with the G2E3
// development baseline. It is not a console-authentication identity.
const std::uint8_t kHidReportDescriptor[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x85, kGamepadReportId,

    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x10,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x10,
    0x81, 0x02,

    0x05, 0x01,
    0x09, 0x39,
    0x15, 0x00,
    0x25, 0x07,
    0x35, 0x00,
    0x46, 0x3B, 0x01,
    0x65, 0x14,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x42,
    0x65, 0x00,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x03,

    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x33,
    0x09, 0x34,
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,

    0x09, 0x32,
    0x09, 0x35,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x02,

    0xC0,
};

constexpr std::uint16_t kConfigTotalLength =
    TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN;

const std::uint8_t kConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        kInterfaceCount,
        0,
        kConfigTotalLength,
        0,
        100
    ),

    TUD_HID_DESCRIPTOR(
        kInterfaceGamepad,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kHidReportDescriptor),
        kEndpointGamepadIn,
        kEndpointSize,
        1
    ),
};

const char* const kStrings[] = {
    nullptr,
    oag::product::kManufacturer,
    oag::product::kProductName,
    "U1-PICO2W",
};

std::uint16_t gStringDescriptor[32] {};

} // namespace

extern "C" std::uint8_t const* tud_descriptor_device_cb(void) {
    return reinterpret_cast<std::uint8_t const*>(&kDeviceDescriptor);
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
    (void)instance;
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

    if (index >= (sizeof(kStrings) / sizeof(kStrings[0]))) {
        return nullptr;
    }

    const char* text = kStrings[index];
    if (text == nullptr) {
        return nullptr;
    }

    const std::size_t length =
        std::min<std::size_t>(std::strlen(text), 31);

    for (std::size_t i = 0; i < length; ++i) {
        gStringDescriptor[1 + i] =
            static_cast<std::uint8_t>(text[i]);
    }

    gStringDescriptor[0] = static_cast<std::uint16_t>(
        (TUSB_DESC_STRING << 8) |
        (2 * length + 2)
    );

    return gStringDescriptor;
}
