/*
 * SPDX-License-Identifier: MIT
 */

#include "drivers/oag/OAGMultiHIDDriver.h"

#include <cstdio>
#include <cstring>

#include "class/hid/hid_device.h"
#include "device/oag_identity.h"
#include "drivers/shared/driverhelper.h"
#include "output/oag_pc_slot_map.h"
#include "output/universal_output_manager.h"
#include "pico/time.h"
#include "pico/unique_id.h"

namespace {

static char serialString[17] = "0000000000000000";

static constexpr uint32_t OAG_HID_KEEPALIVE_MS = 16;

static GamepadState neutralGamepadState() {
    GamepadState state {};
    state.lx = GAMEPAD_JOYSTICK_MID;
    state.ly = GAMEPAD_JOYSTICK_MID;
    state.rx = GAMEPAD_JOYSTICK_MID;
    state.ry = GAMEPAD_JOYSTICK_MID;
    return state;
}

static bool oagHidControlXfer(
    uint8_t rhport,
    uint8_t stage,
    tusb_control_request_t const* request
) {
    return hidd_control_xfer_cb(rhport, stage, request);
}

} // namespace

void OAGMultiHIDDriver::initialize() {
    memset(reports, 0, sizeof(reports));
    memset(lastReports, 0, sizeof(lastReports));
    memset(lastReportValid, 0, sizeof(lastReportValid));
    memset(lastSendMs, 0, sizeof(lastSendMs));

    buildConfigurationDescriptor();

    class_driver = {
#if CFG_TUSB_DEBUG >= 2
        .name = "OAG-MULTI-HID",
#endif
        .init = hidd_init,
        .reset = hidd_reset,
        .open = hidd_open,
        .control_xfer_cb = oagHidControlXfer,
        .xfer_cb = hidd_xfer_cb,
        .sof = nullptr
    };
}

uint8_t OAGMultiHIDDriver::dpadToHat(uint8_t dpad) {
    switch (dpad & GAMEPAD_MASK_DPAD) {
        case GAMEPAD_MASK_UP:
            return 0;
        case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:
            return 1;
        case GAMEPAD_MASK_RIGHT:
            return 2;
        case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT:
            return 3;
        case GAMEPAD_MASK_DOWN:
            return 4;
        case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:
            return 5;
        case GAMEPAD_MASK_LEFT:
            return 6;
        case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:
            return 7;
        default:
            return 8;
    }
}

OAGMultiHIDReport OAGMultiHIDDriver::buildReport(
    GamepadState const& state
) {
    OAGMultiHIDReport report {};

    // Preserve GP2040's normalized button namespace directly.
    report.buttons = state.buttons;
    report.hat = dpadToHat(state.dpad);

    report.lx = static_cast<uint8_t>(state.lx >> 8);
    report.ly = static_cast<uint8_t>(state.ly >> 8);
    report.rx = static_cast<uint8_t>(state.rx >> 8);
    report.ry = static_cast<uint8_t>(state.ry >> 8);
    report.lt = state.lt;
    report.rt = state.rt;

    return report;
}

bool OAGMultiHIDDriver::process(Gamepad* gamepad) {
    (void)gamepad;

    bool anySent = false;

    const uint32_t nowMs =
        static_cast<uint32_t>(
            to_ms_since_boot(get_absolute_time())
        );

    for (
        uint8_t physicalSlot = 0;
        physicalSlot < OAG_MULTI_HID_SLOT_COUNT;
        physicalSlot++
    ) {
        const uint8_t logicalSlot =
            oagPcLogicalSlot(physicalSlot);

        UniversalOutputSlotSnapshot output {};

        const bool hasOutput =
            logicalSlot != 0xFF &&
            UOUTPUT.snapshot(logicalSlot, output) &&
            output.connected &&
            output.hasReport;

        const GamepadState neutral =
            neutralGamepadState();

        const GamepadState& state =
            hasOutput ? output.state : neutral;

        reports[physicalSlot] = buildReport(state);

        const bool changed =
            !lastReportValid[physicalSlot] ||
            memcmp(
                &lastReports[physicalSlot],
                &reports[physicalSlot],
                sizeof(OAGMultiHIDReport)
            ) != 0;

        const bool keepAliveDue =
            !lastReportValid[physicalSlot] ||
            (
                nowMs - lastSendMs[physicalSlot]
            ) >= OAG_HID_KEEPALIVE_MS;

        if (
            (changed || keepAliveDue) &&
            tud_ready() &&
            tud_hid_n_ready(physicalSlot) &&
            tud_hid_n_report(
                physicalSlot,
                0,
                &reports[physicalSlot],
                sizeof(OAGMultiHIDReport)
            )
        ) {
            lastReports[physicalSlot] =
                reports[physicalSlot];

            lastReportValid[physicalSlot] = true;
            lastSendMs[physicalSlot] = nowMs;
            anySent = true;
        }
    }

    return anySent;
}

uint16_t OAGMultiHIDDriver::get_report(
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t* buffer,
    uint16_t reqlen
) {
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    // Normal operation uses interrupt IN endpoints.
    return 0;
}

bool OAGMultiHIDDriver::vendor_control_xfer_cb(
    uint8_t rhport,
    uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

const uint16_t* OAGMultiHIDDriver::get_descriptor_string_cb(
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

    if (index >= 4 && index <= 7) {
        static const char* interfaceNames[] = {
            "OAG Vortex Gamepad 1",
            "OAG Vortex Gamepad 2",
            "OAG Vortex Gamepad 3",
            "OAG Vortex Gamepad 4"
        };

        return getStringDescriptor(
            interfaceNames[index - 4],
            index
        );
    }

    return nullptr;
}

const uint8_t* OAGMultiHIDDriver::get_descriptor_device_cb() {
    return oag_multi_hid_device_descriptor;
}

const uint8_t* OAGMultiHIDDriver::get_hid_descriptor_report_cb(
    uint8_t itf
) {
    if (itf >= OAG_MULTI_HID_SLOT_COUNT) {
        return nullptr;
    }

    return oag_multi_hid_report_descriptor;
}

const uint8_t* OAGMultiHIDDriver::get_descriptor_configuration_cb(
    uint8_t index
) {
    (void)index;
    return configDescriptor;
}

const uint8_t* OAGMultiHIDDriver::get_descriptor_device_qualifier_cb() {
    return nullptr;
}

uint16_t OAGMultiHIDDriver::GetJoystickMidValue() {
    return GAMEPAD_JOYSTICK_MID;
}

void OAGMultiHIDDriver::buildConfigurationDescriptor() {
    uint8_t* out = configDescriptor;

    const uint16_t totalLength = OAG_MULTI_HID_CONFIG_SIZE;

    const uint8_t configHeader[] = {
        0x09,
        0x02,
        static_cast<uint8_t>(totalLength & 0xFF),
        static_cast<uint8_t>((totalLength >> 8) & 0xFF),
        OAG_MULTI_HID_SLOT_COUNT,
        0x01,
        0x00,
        0x80,
        0x32
    };

    memcpy(out, configHeader, sizeof(configHeader));
    out += sizeof(configHeader);

    for (uint8_t slot = 0; slot < OAG_MULTI_HID_SLOT_COUNT; slot++) {
        const uint8_t epIn =
            static_cast<uint8_t>(0x80 | (slot + 1));

        const uint8_t interfaceBlock[] = {
            // Interface
            0x09, 0x04,
            slot,
            0x00,
            0x01,
            0x03,
            0x00,
            0x00,
            static_cast<uint8_t>(4 + slot),

            // HID descriptor
            0x09, 0x21,
            0x11, 0x01,
            0x00,
            0x01,
            0x22,
            static_cast<uint8_t>(
                sizeof(oag_multi_hid_report_descriptor) & 0xFF
            ),
            static_cast<uint8_t>(
                (sizeof(oag_multi_hid_report_descriptor) >> 8) & 0xFF
            ),

            // Interrupt IN endpoint
            0x07, 0x05,
            epIn,
            0x03,
            OAG_MULTI_HID_ENDPOINT_SIZE, 0x00,
            0x01
        };

        memcpy(out, interfaceBlock, sizeof(interfaceBlock));
        out += sizeof(interfaceBlock);
    }
}
