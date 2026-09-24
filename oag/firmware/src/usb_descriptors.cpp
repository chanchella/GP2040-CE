#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/unique_id.h"
#include "tusb.h"

#include "oag/core/product_identity.h"
#include "oag/firmware/pc_xinput_device.h"

namespace {

constexpr std::uint16_t kXinputCompatibilityVid = 0x045E;
constexpr std::uint16_t kXinputCompatibilityPid = 0x028E;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcXinputDevice::kOutputSlots;

// U10B keeps the proven Xbox 360 compatible VID/PID and 20-byte XInput report
// format, but changes the four PC-facing interfaces to the receiver-style
// multi-controller descriptor shape. This is intentionally isolated from all
// physical-input transports (USB Host / Bluetooth) and from OAG routing.
//
// The previous U10A layout repeated the normal wired-controller 0x21 custom
// descriptor four times. Windows XInputGetState exposed four indices, but
// Steam grouped the composite device as one connected controller.
//
// U10B uses the established multi-controller receiver-style 0x22 descriptor:
// one independent interrupt IN/OUT pair per player, with endpoint numbers
// 1/1, 3/3, 5/5 and 7/7 (direction distinguishes IN from OUT).
const std::uint8_t kDeviceDescriptor[] = {
    0x12,
    0x01,
    0x00, 0x02,
    0xFF,
    0xFF,
    0xFF,
    0x40,
    0x5E, 0x04,
    0x8E, 0x02,
    0x16, 0x01, // U10B bcdDevice 1.16
    0x01,
    0x02,
    0x03,
    0x01,
};

const std::uint8_t kConfigurationDescriptor[] = {
    // Configuration: 9 + 4 * (9 + 20 + 7 + 7) = 181 = 0x00B5 bytes.
    0x09, 0x02,
    0xB5, 0x00,
    0x04,
    0x01,
    0x00,
    0xA0,
    0xFA,

    // Player 1 / PC Output 0
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x81, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x01, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,

    // Player 2 / PC Output 1
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x83, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x03, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x03, 0x03, 0x20, 0x00, 0x08,

    // Player 3 / PC Output 2
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x85, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x05, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x05, 0x03, 0x20, 0x00, 0x08,

    // Player 4 / PC Output 3
    0x09, 0x04, 0x03, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x87, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x07, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x87, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x07, 0x03, 0x20, 0x00, 0x08,
};

static_assert(sizeof(kDeviceDescriptor) == 18);
static_assert(kOutputSlots == 4);
static_assert(sizeof(kConfigurationDescriptor) == 0xB5);

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

            // Distinct U10B serial namespace forces a fresh Windows PnP
            // instance instead of relying on stale descriptor caching from
            // the U10A wired-style composite layout.
            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "OAG-RCV-%02X%02X%02X%02X%02X%02X",
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
            return "OAG 4-Player XInput Receiver";
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
