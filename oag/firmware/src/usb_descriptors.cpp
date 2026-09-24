#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/unique_id.h"
#include "tusb.h"

#include "oag/core/product_identity.h"

namespace {

constexpr std::uint16_t kXinputCompatibilityVid = 0x045E;
constexpr std::uint16_t kXinputCompatibilityPid = 0x028E;

const std::uint8_t kDeviceDescriptor[] = {
    0x12,       // bLength
    0x01,       // DEVICE
    0x00, 0x02, // USB 2.00
    0xFF,       // vendor-specific device class
    0xFF,
    0xFF,
    0x40,       // EP0 = 64
    0x5E, 0x04, // compatibility VID 045E
    0x8E, 0x02, // compatibility PID 028E
    0x14, 0x01, // bcdDevice 1.14
    0x01,       // manufacturer
    0x02,       // product
    0x03,       // serial
    0x01,       // one configuration
};

// Xbox 360/XInput-compatible USB layout used by the project's Golden
// XInput driver. Only the gameplay IN/OUT pair is actively used by OAG U2E.
// Audio/plugin/security interfaces are described for Windows XInput binding;
// OAG does not synthesize console authentication in this PC profile.
const std::uint8_t kConfigurationDescriptor[] = {
    // Configuration
    0x09, 0x02,
    0x99, 0x00,
    0x04,
    0x01,
    0x00,
    0xA0,
    0xFA,

    // Interface 0: XInput gameplay/control
    0x09, 0x04,
    0x00, 0x00,
    0x02,
    0xFF, 0x5D, 0x01,
    0x00,

    // XInput gamepad descriptor
    0x11, 0x21,
    0x00, 0x01,
    0x01,
    0x25,
    0x81,
    0x14,
    0x00, 0x00, 0x00, 0x00, 0x13,
    0x02,
    0x08,
    0x00, 0x00,

    // Gameplay IN
    0x07, 0x05,
    0x81,
    0x03,
    0x20, 0x00,
    0x01,

    // Gameplay OUT
    0x07, 0x05,
    0x02,
    0x03,
    0x20, 0x00,
    0x08,

    // Interface 1: XInput audio compatibility interface
    0x09, 0x04,
    0x01, 0x00,
    0x04,
    0xFF, 0x5D, 0x03,
    0x00,

    0x1B, 0x21,
    0x00, 0x01, 0x01, 0x01,
    0x83, 0x40, 0x01,
    0x04, 0x20, 0x16,
    0x85,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x16,
    0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x40,
    0x07, 0x05, 0x06, 0x03, 0x20, 0x00, 0x10,

    // Interface 2: XInput plugin module compatibility interface
    0x09, 0x04,
    0x02, 0x00,
    0x01,
    0xFF, 0x5D, 0x02,
    0x00,

    0x09, 0x21,
    0x00, 0x01,
    0x01,
    0x22,
    0x86,
    0x03,
    0x00,

    0x07, 0x05,
    0x86,
    0x03,
    0x20, 0x00,
    0x10,

    // Interface 3: security interface descriptor.
    // No console authentication provider is active in U2E.
    0x09, 0x04,
    0x03, 0x00,
    0x00,
    0xFF, 0xFD, 0x13,
    0x04,

    0x06, 0x41,
    0x00, 0x01, 0x01, 0x03,
};

static_assert(sizeof(kDeviceDescriptor) == 18);
static_assert(sizeof(kConfigurationDescriptor) == 0x99);

std::uint16_t gStringDescriptor[32] {};
char gSerial[24] {};

const char* stringValue(std::uint8_t index) {
    switch (index) {
        case 1:
            return oag::product::kManufacturer;
        case 2:
            return oag::product::kProductName;
        case 3: {
            pico_unique_board_id_t id {};
            pico_get_unique_board_id(&id);

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "OAG-%02X%02X%02X%02X%02X%02X",
                id.id[2],
                id.id[3],
                id.id[4],
                id.id[5],
                id.id[6],
                id.id[7]
            );
            return gSerial;
        }
        case 4:
            return "OAG XInput Compatibility";
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
        std::min<std::size_t>(std::strlen(text), 31);

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
