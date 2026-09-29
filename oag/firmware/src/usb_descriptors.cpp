#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/unique_id.h"
#include "tusb.h"

#include "oag/core/product_identity.h"
#include "oag/firmware/output_profile_selector.h"
#include "oag/firmware/pc_hid_output.h"
#include "oag/firmware/pc_xinput_device.h"
#include "oag/firmware/windows_xusb20_compat.h"

namespace {


constexpr std::uint16_t kPcReceiverVid = 0xCAFE;
constexpr std::uint16_t kPcReceiverPid = 0x401A;
constexpr std::uint8_t kPcMsOsVendorCode = 0x90;
constexpr std::size_t kPcOutputSlots =
    oag::firmware::PcXinputDevice::kOutputSlots;

const std::uint8_t kPcKeyboardReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const std::uint8_t kPcMouseReportDescriptor[] = {
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
const std::uint8_t kPcDeviceDescriptor[] = {
    0x12, 0x01,
    0x00, 0x02,
    TUSB_CLASS_MISC, MISC_SUBCLASS_COMMON, MISC_PROTOCOL_IAD,
    0x08,
    0xFE, 0xCA,
    0x1A, 0x40,
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01,
};

const std::uint8_t kPcConfigurationDescriptor[] = {
    // Golden receiver + native KM + controller microphone UAC2.
    // 371-byte receiver/KM topology + TinyUSB one-channel microphone function.
    0x09, 0x02,
    static_cast<std::uint8_t>((0x0173 + TUD_AUDIO_MIC_ONE_CH_DESC_LEN) & 0xFFu),
    static_cast<std::uint8_t>((0x0173 + TUD_AUDIO_MIC_ONE_CH_DESC_LEN) >> 8),
    0x0C,
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

    // Native keyboard — interface 8 — EP 89
    TUD_HID_DESCRIPTOR(
        0x08,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(kPcKeyboardReportDescriptor),
        0x89,
        8,
        1
    ),

    // Native mouse — interface 9 — EP 8A
    TUD_HID_DESCRIPTOR(
        0x09,
        0,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(kPcMouseReportDescriptor),
        0x8A,
        8,
        1
    ),

    // Controller headset microphone bridge — interfaces 10/11 — EP 8B.
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(
        0x0A,
        0,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
        16,
        0x8B,
        CFG_TUD_AUDIO_EP_SZ_IN
    ),
};

static_assert(sizeof(kPcDeviceDescriptor) == 18);
static_assert(kPcOutputSlots == 4);
static_assert(sizeof(kPcConfigurationDescriptor) == 0x0173 + TUD_AUDIO_MIC_ONE_CH_DESC_LEN);

alignas(2) const std::uint8_t kPcMsOsStringDescriptor[] = {
    0x12, 0x03,
    0x4D, 0x00, 0x53, 0x00, 0x46, 0x00,
    0x54, 0x00, 0x31, 0x00, 0x30, 0x00, 0x30, 0x00,
    kPcMsOsVendorCode,
    0x00,
};

// Microsoft OS 1.0 Extended Compatible ID.
// Preserve UI5G's standard USB topology exactly. Expose each receiver
// controller/auxiliary interface pair as its own XUSB20 function so Windows
// can bind four independent receiver functions while interfaces 8/9 remain
// ordinary HID keyboard/mouse functions.
const std::uint8_t kPcExtendedCompatIdDescriptor[] = {
    0x70, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00,
    0x02,
    0x58, 0x55, 0x53, 0x42, 0x32, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x02,
    0x02,
    0x58, 0x55, 0x53, 0x42, 0x32, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x04,
    0x02,
    0x58, 0x55, 0x53, 0x42, 0x32, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x06,
    0x02,
    0x58, 0x55, 0x53, 0x42, 0x32, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static_assert(sizeof(kPcMsOsStringDescriptor) == 18);
static_assert(sizeof(kPcExtendedCompatIdDescriptor) == 0x70);




constexpr std::uint16_t kMobileDeviceVid = 0xCAFE;
constexpr std::uint16_t kMobileDevicePid = 0x4017;
constexpr std::size_t kMobileOutputSlots =
    oag::firmware::PcHidOutput::kOutputSlots;

constexpr std::uint8_t kMobileGamepadEndpointSize = 16;
constexpr std::uint8_t kMobileKmEndpointSize = 8;
constexpr std::uint8_t kMobilePollingIntervalMs = 1;

const std::uint8_t kMobileGamepadReportDescriptor[] = {
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

    // Canonical 16-button Android/Linux/TinyUSB bitmap.
    // D-pad is intentionally Hat-only; it is not duplicated as buttons.
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (Button 1)
    0x29, 0x10,       //   Usage Maximum (Button 16)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x10,       //   Report Count (16)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    0xC0,             // End Collection
};

const std::uint8_t kMobileKeyboardReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const std::uint8_t kMobileMouseReportDescriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

const std::uint8_t kMobileDeviceDescriptor[] = {
    0x12, 0x01,             // bLength, bDescriptorType
    0x00, 0x02,             // USB 2.00
    0x00, 0x00, 0x00,       // class/subclass/protocol per interface
    0x08,                   // EP0 = 8; shared with UI5K PC persona
    static_cast<std::uint8_t>(kMobileDeviceVid & 0xFFu),
    static_cast<std::uint8_t>(kMobileDeviceVid >> 8),
    static_cast<std::uint8_t>(kMobileDevicePid & 0xFFu),
    static_cast<std::uint8_t>(kMobileDevicePid >> 8),
    0x00, 0x01,             // bcdDevice 1.00
    0x01, 0x02, 0x03,       // manufacturer/product/serial
    0x01,                   // one configuration
};

constexpr std::uint8_t kMobileInterfaceCount = 6;
constexpr std::uint16_t kMobileConfigurationLength =
    TUD_CONFIG_DESC_LEN +
    static_cast<std::uint16_t>(kMobileInterfaceCount) * TUD_HID_DESC_LEN;

const std::uint8_t kMobileConfigurationDescriptor[] = {
    // Configuration: four independent standard HID gamepads followed by the
    // Golden native keyboard and mouse interfaces.
    TUD_CONFIG_DESCRIPTOR(
        1,
        kMobileInterfaceCount,
        0,
        kMobileConfigurationLength,
        0x00,
        100
    ),

    // Gamepad 1 — HID instance 0 — interface 0 — EP 81
    TUD_HID_DESCRIPTOR(
        0,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kMobileGamepadReportDescriptor),
        0x81,
        kMobileGamepadEndpointSize,
        kMobilePollingIntervalMs
    ),

    // Gamepad 2 — HID instance 1 — interface 1 — EP 82
    TUD_HID_DESCRIPTOR(
        1,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kMobileGamepadReportDescriptor),
        0x82,
        kMobileGamepadEndpointSize,
        kMobilePollingIntervalMs
    ),

    // Gamepad 3 — HID instance 2 — interface 2 — EP 83
    TUD_HID_DESCRIPTOR(
        2,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kMobileGamepadReportDescriptor),
        0x83,
        kMobileGamepadEndpointSize,
        kMobilePollingIntervalMs
    ),

    // Gamepad 4 — HID instance 3 — interface 3 — EP 84
    TUD_HID_DESCRIPTOR(
        3,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(kMobileGamepadReportDescriptor),
        0x84,
        kMobileGamepadEndpointSize,
        kMobilePollingIntervalMs
    ),

    // Native keyboard — HID instance 4 — interface 4 — EP 85
    TUD_HID_DESCRIPTOR(
        4,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(kMobileKeyboardReportDescriptor),
        0x85,
        kMobileKmEndpointSize,
        kMobilePollingIntervalMs
    ),

    // Native mouse — HID instance 5 — interface 5 — EP 86
    TUD_HID_DESCRIPTOR(
        5,
        0,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(kMobileMouseReportDescriptor),
        0x86,
        kMobileKmEndpointSize,
        kMobilePollingIntervalMs
    ),
};

static_assert(sizeof(kMobileDeviceDescriptor) == 18);
static_assert(kMobileOutputSlots == 4);
static_assert(kMobileInterfaceCount == 6);
static_assert(sizeof(kMobileConfigurationDescriptor) == kMobileConfigurationLength);



std::uint16_t gStringDescriptor[64] {};
char gSerial[32] {};

bool mobileProfile() {
    return
        oag::firmware::activeOutputProfile() ==
        oag::firmware::OutputProfileId::Phone;
}

const char* stringValue(std::uint8_t index) {
    switch (index) {
        case 1:
            return oag::product::kManufacturer;

        case 2:
            // Branding only. Keep the PC XUSB child-controller identity,
            // VID/PID, interfaces, endpoints and compatibility descriptors
            // untouched; this changes only the USB device Product String.
            return "OAG ABO GEMI ULTRA GAMING";

        case 3: {
            pico_unique_board_id_t id {};
            pico_get_unique_board_id(&id);

            std::snprintf(
                gSerial,
                sizeof(gSerial),
                mobileProfile()
                    ? "OAG-MOB-%02X%02X%02X%02X%02X%02X"
                    : "AOG-XI2-%02X%02X%02X%02X%02X%02X",
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
    return mobileProfile()
        ? kMobileDeviceDescriptor
        : kPcDeviceDescriptor;
}

extern "C" std::uint8_t const* tud_descriptor_configuration_cb(
    std::uint8_t index
) {
    (void)index;

    return mobileProfile()
        ? kMobileConfigurationDescriptor
        : kPcConfigurationDescriptor;
}

extern "C" std::uint8_t const* tud_hid_descriptor_report_cb(
    std::uint8_t instance
) {
    if (mobileProfile()) {
        if (instance < kMobileOutputSlots) {
            return kMobileGamepadReportDescriptor;
        }

        switch (instance) {
            case 4:
                return kMobileKeyboardReportDescriptor;
            case 5:
                return kMobileMouseReportDescriptor;
            default:
                return nullptr;
        }
    }

    switch (instance) {
        case 0:
            return kPcKeyboardReportDescriptor;
        case 1:
            return kPcMouseReportDescriptor;
        default:
            return nullptr;
    }
}

extern "C" std::uint16_t const* tud_descriptor_string_cb(
    std::uint8_t index,
    std::uint16_t langid
) {
    (void)langid;

    if (!mobileProfile() && index == 0xEE) {
        return reinterpret_cast<std::uint16_t const*>(
            kPcMsOsStringDescriptor
        );
    }

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

namespace oag::firmware {

bool handleWindowsXusb20CompatIdRequest(
    std::uint8_t rhport,
    tusb_control_request_t const* request
) {
    if (mobileUsbProfileActive()) {
        return false;
    }

    if (
        request == nullptr ||
        request->bmRequestType != 0xC0 ||
        request->bRequest != kPcMsOsVendorCode ||
        request->wValue != 0x0000 ||
        request->wIndex != 0x0004
    ) {
        return false;
    }

    const std::uint16_t transferLength =
        std::min<std::uint16_t>(
            request->wLength,
            static_cast<std::uint16_t>(
                sizeof(kPcExtendedCompatIdDescriptor)
            )
        );

    return tud_control_xfer(
        rhport,
        request,
        const_cast<std::uint8_t*>(
            kPcExtendedCompatIdDescriptor
        ),
        transferLength
    );
}

} // namespace oag::firmware
