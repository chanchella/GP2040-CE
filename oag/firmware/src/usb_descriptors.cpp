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

// U10D keeps the complete U10C input/routing behavior and changes only the
// PC-facing multi-controller descriptor shape.
//
// The independent layout follows the known multi-interface Xbox 360 gadget
// pattern:
//   P1 = IN 0x81 / OUT 0x01
//   P2 = IN 0x82 / OUT 0x02
//   P3 = IN 0x83 / OUT 0x03
//   P4 = IN 0x84 / OUT 0x04
//
// Each interface owns a complete interrupt IN/OUT pair and uses the 0x22
// custom descriptor form. Input remains the proven 20-byte Xbox 360 report;
// reverse rumble remains the proven 8-byte report.
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
    0x17, 0x01, // U10D bcdDevice 1.17: force fresh Windows enumeration.
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
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x04,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x81, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x01, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,

    // Player 2 / PC Output 1
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x05,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x82, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x02, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x82, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x08,

    // Player 3 / PC Output 2
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x06,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x83, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x03, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x03, 0x03, 0x20, 0x00, 0x08,

    // Player 4 / PC Output 3
    0x09, 0x04, 0x03, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x07,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x84, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x04, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x84, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x08,
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

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "OAG-IND-%02X%02X%02X%02X%02X%02X",
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
            return "OAG Independent Player 1";
        case 5:
            return "OAG Independent Player 2";
        case 6:
            return "OAG Independent Player 3";
        case 7:
            return "OAG Independent Player 4";
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
