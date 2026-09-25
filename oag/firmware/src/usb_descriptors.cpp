#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/unique_id.h"
#include "tusb.h"

#include "oag/core/product_identity.h"
#include "oag/firmware/pc_xinput_device.h"
#include "oag/firmware/target_usb_persona.h"

namespace {

constexpr std::uint16_t kReceiverVid = 0x045E;
constexpr std::uint16_t kReceiverPid = 0x0719;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcXinputDevice::kOutputSlots;

const std::uint8_t kKeyboardReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const std::uint8_t kMouseReportDescriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

// U10E emulates the full-speed Microsoft Xbox 360 Wireless Receiver USB
// topology instead of repeating wired-controller interfaces.
//
// Genuine 045E:0719 topology:
//   interface 0 protocol 0x81 -> controller 1 EP 81/01
//   interface 1 protocol 0x82 -> auxiliary    EP 82/02
//   interface 2 protocol 0x81 -> controller 2 EP 83/03
//   interface 3 protocol 0x82 -> auxiliary    EP 84/04
//   interface 4 protocol 0x81 -> controller 3 EP 85/05
//   interface 5 protocol 0x82 -> auxiliary    EP 86/06
//   interface 6 protocol 0x81 -> controller 4 EP 87/07
//   interface 7 protocol 0x82 -> auxiliary    EP 88/08
//
// The configuration below is byte-shaped from real 045E:0719 descriptor
// captures. Gamepad interfaces use the 20-byte 0x22 receiver descriptor;
// auxiliary interfaces use the 12-byte 0x22 descriptor.
const std::uint8_t kControllerDeviceDescriptor[] = {
    0x12, 0x01,
    0x00, 0x02,
    0xFF, 0xFF, 0xFF,
    0x08,
    0x5E, 0x04,
    0x19, 0x07,
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01,
};

const std::uint8_t kControllerConfigurationDescriptor[] = {
    // Exact Xbox 360 Wireless Receiver controller-only topology.
    0x09, 0x02, 0x41, 0x01,
    0x08,
    0x01,
    0x00,
    0xA0,
    0x82, // 260 mA

    // Controller 1 — interface 0 — EP 81 / 01
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x81, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x81, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x01, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,

    // Controller 1 auxiliary — interface 1 — EP 82 / 02
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x5D, 0x82, 0x00,
    0x0C, 0x22, 0x00, 0x01, 0x01, 0x82, 0x00, 0x40,
    0x01, 0x02, 0x20, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x04,

    // Controller 2 — interface 2 — EP 83 / 03
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x5D, 0x81, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x83, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x03, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x03, 0x03, 0x20, 0x00, 0x08,

    // Controller 2 auxiliary — interface 3 — EP 84 / 04
    0x09, 0x04, 0x03, 0x00, 0x02, 0xFF, 0x5D, 0x82, 0x00,
    0x0C, 0x22, 0x00, 0x01, 0x01, 0x84, 0x00, 0x40,
    0x01, 0x04, 0x20, 0x00,
    0x07, 0x05, 0x84, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x04,

    // Controller 3 — interface 4 — EP 85 / 05
    0x09, 0x04, 0x04, 0x00, 0x02, 0xFF, 0x5D, 0x81, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x85, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x05, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x05, 0x03, 0x20, 0x00, 0x08,

    // Controller 3 auxiliary — interface 5 — EP 86 / 06
    0x09, 0x04, 0x05, 0x00, 0x02, 0xFF, 0x5D, 0x82, 0x00,
    0x0C, 0x22, 0x00, 0x01, 0x01, 0x86, 0x00, 0x40,
    0x01, 0x06, 0x20, 0x00,
    0x07, 0x05, 0x86, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x06, 0x03, 0x20, 0x00, 0x04,

    // Controller 4 — interface 6 — EP 87 / 07
    0x09, 0x04, 0x06, 0x00, 0x02, 0xFF, 0x5D, 0x81, 0x00,
    0x14, 0x22, 0x00, 0x01, 0x13, 0x87, 0x1D, 0x00,
    0x17, 0x01, 0x02, 0x08, 0x13, 0x07, 0x0C, 0x00,
    0x0C, 0x01, 0x02, 0x08,
    0x07, 0x05, 0x87, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x07, 0x03, 0x20, 0x00, 0x08,

    // Controller 4 auxiliary — interface 7 — EP 88 / 08
    0x09, 0x04, 0x07, 0x00, 0x02, 0xFF, 0x5D, 0x82, 0x00,
    0x0C, 0x22, 0x00, 0x01, 0x01, 0x88, 0x00, 0x40,
    0x01, 0x08, 0x20, 0x00,
    0x07, 0x05, 0x88, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x08, 0x03, 0x20, 0x00, 0x04,

};

const std::uint8_t kNativeKmDeviceDescriptor[] = {
    0x12, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00,
    0x08,
    0xFE, 0xCA,
    0x14, 0x40,
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01,
};

const std::uint8_t kNativeKmConfigurationDescriptor[] = {
    0x09, 0x02, 0x3B, 0x00,
    0x02,
    0x01,
    0x00,
    0xA0,
    0x32,

    TUD_HID_DESCRIPTOR(
        0x00,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(kKeyboardReportDescriptor),
        0x81,
        8,
        1
    ),

    TUD_HID_DESCRIPTOR(
        0x01,
        0,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(kMouseReportDescriptor),
        0x82,
        8,
        1
    ),
};

static_assert(sizeof(kControllerDeviceDescriptor) == 18);
static_assert(sizeof(kNativeKmDeviceDescriptor) == 18);
static_assert(kOutputSlots == 4);
static_assert(sizeof(kControllerConfigurationDescriptor) == 0x0141);
static_assert(sizeof(kNativeKmConfigurationDescriptor) == 0x003B);

oag::firmware::TargetUsbPersona gTargetUsbPersona =
    oag::firmware::TargetUsbPersona::NativeKeyboardMouse;

std::uint16_t gStringDescriptor[32] {};
char gSerial[24] {};

const char* stringValue(std::uint8_t index) {
    switch (index) {
        case 1:
            return oag::product::kManufacturer;
        case 2:
            return
                gTargetUsbPersona ==
                    oag::firmware::TargetUsbPersona::NativeKeyboardMouse
                    ? "AOG Abo Gemi Keyboard Mouse"
                    : "AOG Abo Gemi ultra gaming";
        case 3: {
            pico_unique_board_id_t id {};
            pico_get_unique_board_id(&id);

            const char* prefix =
                gTargetUsbPersona ==
                    oag::firmware::TargetUsbPersona::NativeKeyboardMouse
                    ? "AOG-KM-"
                    : "AOG-XI2-";

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                "%s%02X%02X%02X%02X%02X%02X",
                prefix,
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
    return
        gTargetUsbPersona ==
            oag::firmware::TargetUsbPersona::NativeKeyboardMouse
            ? kNativeKmDeviceDescriptor
            : kControllerDeviceDescriptor;
}

extern "C" std::uint8_t const* tud_descriptor_configuration_cb(
    std::uint8_t index
) {
    (void)index;

    return
        gTargetUsbPersona ==
            oag::firmware::TargetUsbPersona::NativeKeyboardMouse
            ? kNativeKmConfigurationDescriptor
            : kControllerConfigurationDescriptor;
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


extern "C" std::uint8_t const* tud_hid_descriptor_report_cb(
    std::uint8_t instance
) {
    switch (instance) {
        case 0:
            return kKeyboardReportDescriptor;
        case 1:
            return kMouseReportDescriptor;
        default:
            return nullptr;
    }
}

extern "C" std::uint16_t tud_hid_get_report_cb(
    std::uint8_t instance,
    std::uint8_t reportId,
    hid_report_type_t reportType,
    std::uint8_t* buffer,
    std::uint16_t requestedLength
) {
    (void)instance;
    (void)reportId;
    (void)reportType;
    (void)buffer;
    (void)requestedLength;
    return 0;
}

extern "C" void tud_hid_set_report_cb(
    std::uint8_t instance,
    std::uint8_t reportId,
    hid_report_type_t reportType,
    std::uint8_t const* buffer,
    std::uint16_t bufferSize
) {
    (void)instance;
    (void)reportId;
    (void)reportType;
    (void)buffer;
    (void)bufferSize;
}


namespace oag::firmware {

void setTargetUsbPersona(TargetUsbPersona persona) {
    gTargetUsbPersona = persona;
}

TargetUsbPersona targetUsbPersona() {
    return gTargetUsbPersona;
}

} // namespace oag::firmware
