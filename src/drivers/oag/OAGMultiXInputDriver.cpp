/*
 * SPDX-License-Identifier: MIT
 *
 * Multi-controller XInput device output for OAG Vortex.
 *
 * Descriptor topology derived from the MIT-licensed
 * CasperVM/360-w-raw-gadget project and rewritten for TinyUSB.
 */

#include "drivers/oag/OAGMultiXInputDriver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "device/oag_identity.h"
#include "drivers/shared/driverhelper.h"
#include "output/universal_output_manager.h"
#include "pico/unique_id.h"

namespace {

static uint8_t endpointIn[OAG_MULTI_XINPUT_SLOT_COUNT] {};
static uint8_t endpointOut[OAG_MULTI_XINPUT_SLOT_COUNT] {};

static uint8_t outBuffers[OAG_MULTI_XINPUT_SLOT_COUNT]
                         [OAG_MULTI_XINPUT_ENDPOINT_SIZE] {};

static XInputReport txReports[OAG_MULTI_XINPUT_SLOT_COUNT] {};
static XInputReport lastReports[OAG_MULTI_XINPUT_SLOT_COUNT] {};
static bool lastReportValid[OAG_MULTI_XINPUT_SLOT_COUNT] {};

static char serialString[17] = "0000000000000000";

static void resetUsbState() {
    memset(endpointIn, 0, sizeof(endpointIn));
    memset(endpointOut, 0, sizeof(endpointOut));
    memset(outBuffers, 0, sizeof(outBuffers));
    memset(txReports, 0, sizeof(txReports));
    memset(lastReports, 0, sizeof(lastReports));
    memset(lastReportValid, 0, sizeof(lastReportValid));
}

static void multiXinputInit() {
    resetUsbState();
}

static void multiXinputReset(uint8_t rhport) {
    (void)rhport;
    resetUsbState();
}

static uint16_t multiXinputOpen(
    uint8_t rhport,
    tusb_desc_interface_t const* itf,
    uint16_t maxLength
) {
    if (
        itf == nullptr ||
        itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        itf->bInterfaceSubClass != 0x5D ||
        itf->bInterfaceProtocol != 0x01 ||
        itf->bNumEndpoints != 2 ||
        itf->bInterfaceNumber >= OAG_MULTI_XINPUT_SLOT_COUNT
    ) {
        return 0;
    }

    constexpr uint16_t blockLength = 43;
    TU_VERIFY(maxLength >= blockLength, 0);

    uint8_t const* cursor =
        reinterpret_cast<uint8_t const*>(itf) + sizeof(tusb_desc_interface_t);

    // Xbox vendor descriptor used by the multi-interface layout.
    if (cursor[0] != 0x14 || cursor[1] != 0x22) {
        return 0;
    }

    cursor += cursor[0];

    const uint8_t slot = itf->bInterfaceNumber;

    uint8_t epOut = 0;
    uint8_t epIn = 0;

    TU_ASSERT(
        usbd_open_edpt_pair(
            rhport,
            cursor,
            itf->bNumEndpoints,
            TUSB_XFER_INTERRUPT,
            &epOut,
            &epIn
        ),
        0
    );

    endpointOut[slot] = epOut;
    endpointIn[slot] = epIn;

    return blockLength;
}

static bool multiXinputControlRequest(
    uint8_t rhport,
    uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;
    return true;
}

static bool multiXinputControlComplete(
    uint8_t rhport,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)request;
    return true;
}

static int8_t slotForOutEndpoint(uint8_t epAddr) {
    for (uint8_t i = 0; i < OAG_MULTI_XINPUT_SLOT_COUNT; i++) {
        if (endpointOut[i] != 0 && endpointOut[i] == epAddr) {
            return static_cast<int8_t>(i);
        }
    }

    return -1;
}

static bool multiXinputXferCallback(
    uint8_t rhport,
    uint8_t epAddr,
    xfer_result_t result,
    uint32_t xferredBytes
) {
    (void)xferredBytes;

    if (result != XFER_RESULT_SUCCESS) {
        return true;
    }

    const int8_t slot = slotForOutEndpoint(epAddr);
    if (slot < 0) {
        return true;
    }

    // Keep the endpoint continuously armed for per-slot rumble / LED data.
    usbd_edpt_xfer(
        rhport,
        endpointOut[slot],
        outBuffers[slot],
        OAG_MULTI_XINPUT_ENDPOINT_SIZE
    );

    return true;
}

static XInputReport buildXinputReport(GamepadState const& state) {
    XInputReport report {};

    report.report_id = 0x00;
    report.report_size = XINPUT_ENDPOINT_SIZE;

    report.buttons1 =
        ((state.dpad & GAMEPAD_MASK_UP)    ? XBOX_MASK_UP    : 0) |
        ((state.dpad & GAMEPAD_MASK_DOWN)  ? XBOX_MASK_DOWN  : 0) |
        ((state.dpad & GAMEPAD_MASK_LEFT)  ? XBOX_MASK_LEFT  : 0) |
        ((state.dpad & GAMEPAD_MASK_RIGHT) ? XBOX_MASK_RIGHT : 0) |
        ((state.buttons & GAMEPAD_MASK_S2) ? XBOX_MASK_START : 0) |
        ((state.buttons & GAMEPAD_MASK_S1) ? XBOX_MASK_BACK  : 0) |
        ((state.buttons & GAMEPAD_MASK_L3) ? XBOX_MASK_LS    : 0) |
        ((state.buttons & GAMEPAD_MASK_R3) ? XBOX_MASK_RS    : 0);

    report.buttons2 =
        ((state.buttons & GAMEPAD_MASK_L1) ? XBOX_MASK_LB   : 0) |
        ((state.buttons & GAMEPAD_MASK_R1) ? XBOX_MASK_RB   : 0) |
        ((state.buttons & GAMEPAD_MASK_A1) ? XBOX_MASK_HOME : 0) |
        ((state.buttons & GAMEPAD_MASK_B1) ? XBOX_MASK_A    : 0) |
        ((state.buttons & GAMEPAD_MASK_B2) ? XBOX_MASK_B    : 0) |
        ((state.buttons & GAMEPAD_MASK_B3) ? XBOX_MASK_X    : 0) |
        ((state.buttons & GAMEPAD_MASK_B4) ? XBOX_MASK_Y    : 0);

    report.lt = state.lt;
    report.rt = state.rt;

    if ((state.buttons & GAMEPAD_MASK_L2) && report.lt == 0) {
        report.lt = 0xFF;
    }

    if ((state.buttons & GAMEPAD_MASK_R2) && report.rt == 0) {
        report.rt = 0xFF;
    }

    report.lx = static_cast<int16_t>(state.lx) + INT16_MIN;
    report.ly = static_cast<int16_t>(~state.ly) + INT16_MIN;
    report.rx = static_cast<int16_t>(state.rx) + INT16_MIN;
    report.ry = static_cast<int16_t>(~state.ry) + INT16_MIN;

    return report;
}

static XInputReport neutralReport() {
    GamepadState state {};
    state.lx = GAMEPAD_JOYSTICK_MID;
    state.ly = GAMEPAD_JOYSTICK_MID;
    state.rx = GAMEPAD_JOYSTICK_MID;
    state.ry = GAMEPAD_JOYSTICK_MID;
    return buildXinputReport(state);
}

static void armOutEndpoint(uint8_t slot) {
    if (
        !tud_ready() ||
        slot >= OAG_MULTI_XINPUT_SLOT_COUNT ||
        endpointOut[slot] == 0 ||
        usbd_edpt_busy(0, endpointOut[slot])
    ) {
        return;
    }

    usbd_edpt_claim(0, endpointOut[slot]);
    usbd_edpt_xfer(
        0,
        endpointOut[slot],
        outBuffers[slot],
        OAG_MULTI_XINPUT_ENDPOINT_SIZE
    );
    usbd_edpt_release(0, endpointOut[slot]);
}

} // namespace

void OAGMultiXInputDriver::initialize() {
    resetUsbState();
    buildConfigurationDescriptor();

    class_driver = {
#if CFG_TUSB_DEBUG >= 2
        .name = "OAG-MULTI-XINPUT",
#endif
        .init = multiXinputInit,
        .reset = multiXinputReset,
        .open = multiXinputOpen,
        .control_xfer_cb = multiXinputControlRequest,
        .xfer_cb = multiXinputXferCallback,
        .sof = nullptr
    };
}

bool OAGMultiXInputDriver::process(Gamepad* gamepad) {
    (void)gamepad;

    bool anySent = false;

    for (uint8_t slot = 0; slot < OAG_MULTI_XINPUT_SLOT_COUNT; slot++) {
        UniversalOutputSlotSnapshot output {};
        const bool hasOutput =
            UOUTPUT.snapshot(slot, output) &&
            output.connected &&
            output.hasReport;

        const XInputReport report =
            hasOutput ? buildXinputReport(output.state) : neutralReport();

        if (
            endpointIn[slot] != 0 &&
            tud_ready() &&
            !usbd_edpt_busy(0, endpointIn[slot]) &&
            (
                !lastReportValid[slot] ||
                memcmp(
                    &lastReports[slot],
                    &report,
                    sizeof(XInputReport)
                ) != 0
            )
        ) {
            // TinyUSB completes asynchronously, so the transfer buffer
            // must outlive this process() iteration.
            txReports[slot] = report;

            usbd_edpt_claim(0, endpointIn[slot]);
            const bool queued = usbd_edpt_xfer(
                0,
                endpointIn[slot],
                reinterpret_cast<uint8_t*>(&txReports[slot]),
                sizeof(XInputReport)
            );
            usbd_edpt_release(0, endpointIn[slot]);

            if (queued) {
                lastReports[slot] = report;
                lastReportValid[slot] = true;
                anySent = true;
            }
        }

        armOutEndpoint(slot);
    }

    return anySent;
}

uint16_t OAGMultiXInputDriver::get_report(
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t* buffer,
    uint16_t reqlen
) {
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

bool OAGMultiXInputDriver::vendor_control_xfer_cb(
    uint8_t rhport,
    uint8_t stage,
    tusb_control_request_t const* request
) {
    // Multi-interface XUSB implementations are probed with vendor requests
    // on some hosts. Acknowledge them without inventing authentication data.
    if (stage == CONTROL_STAGE_SETUP) {
        const uint16_t length =
            std::min<uint16_t>(request->wLength, sizeof(controlBuffer));

        memset(controlBuffer, 0, length);
        return tud_control_xfer(
            rhport,
            request,
            controlBuffer,
            length
        );
    }

    return true;
}

const uint16_t* OAGMultiXInputDriver::get_descriptor_string_cb(
    uint8_t index,
    uint16_t langid
) {
    (void)langid;

    if (index == 0) {
        static uint16_t language[] = {
            static_cast<uint16_t>((TUSB_DESC_STRING << 8) | 4),
            0x0409
        };
        return language;
    }

    if (index == 1) {
        return getStringDescriptor(OAG_USB_MANUFACTURER_STRING, index);
    }

    if (index == 2) {
        return getStringDescriptor(OAG_USB_PRODUCT_STRING, index);
    }

    if (index == 3) {
        pico_unique_board_id_t id {};
        pico_get_unique_board_id(&id);

        snprintf(
            serialString,
            sizeof(serialString),
            "%02X%02X%02X%02X%02X%02X%02X%02X",
            id.id[0], id.id[1], id.id[2], id.id[3],
            id.id[4], id.id[5], id.id[6], id.id[7]
        );

        return getStringDescriptor(serialString, index);
    }

    return nullptr;
}

const uint8_t* OAGMultiXInputDriver::get_descriptor_device_cb() {
    return oag_multi_xinput_device_descriptor;
}

const uint8_t* OAGMultiXInputDriver::get_hid_descriptor_report_cb(
    uint8_t itf
) {
    (void)itf;
    return nullptr;
}

const uint8_t* OAGMultiXInputDriver::get_descriptor_configuration_cb(
    uint8_t index
) {
    (void)index;
    return configDescriptor;
}

const uint8_t* OAGMultiXInputDriver::get_descriptor_device_qualifier_cb() {
    return nullptr;
}

uint16_t OAGMultiXInputDriver::GetJoystickMidValue() {
    return GAMEPAD_JOYSTICK_MID;
}

void OAGMultiXInputDriver::buildConfigurationDescriptor() {
    uint8_t* out = configDescriptor;

    const uint16_t totalLength = OAG_MULTI_XINPUT_CONFIG_SIZE;

    const uint8_t configHeader[] = {
        0x09,
        0x02,
        static_cast<uint8_t>(totalLength & 0xFF),
        static_cast<uint8_t>((totalLength >> 8) & 0xFF),
        OAG_MULTI_XINPUT_SLOT_COUNT,
        0x01,
        0x00,
        0xA0,
        0xFA
    };

    memcpy(out, configHeader, sizeof(configHeader));
    out += sizeof(configHeader);

    for (uint8_t slot = 0; slot < OAG_MULTI_XINPUT_SLOT_COUNT; slot++) {
        const uint8_t epIn =
            static_cast<uint8_t>(0x80 | (slot + 1));
        const uint8_t epOut =
            static_cast<uint8_t>(slot + 1);

        const uint8_t interfaceBlock[] = {
            // Interface descriptor
            0x09, 0x04,
            slot,
            0x00,
            0x02,
            0xFF,
            0x5D,
            0x01,
            0x00,

            // Xbox vendor descriptor
            0x14, 0x22,
            0x00, 0x01, 0x13,
            epIn,
            0x1D, 0x00, 0x17, 0x01, 0x02, 0x08, 0x13,
            epOut,
            0x0C, 0x00, 0x0C, 0x01, 0x02, 0x08,

            // IN endpoint
            0x07, 0x05,
            epIn,
            0x03,
            0x20, 0x00,
            0x04,

            // OUT endpoint
            0x07, 0x05,
            epOut,
            0x03,
            0x20, 0x00,
            0x08
        };

        memcpy(out, interfaceBlock, sizeof(interfaceBlock));
        out += sizeof(interfaceBlock);
    }
}
