/*
 * SPDX-License-Identifier: MIT
 */

#include "drivers/oag/OAGMultiHIDDriver.h"

#include <cstdio>
#include <cstring>

#include "class/hid/hid_device.h"
#include "device/oag_identity.h"
#include "drivers/shared/driverhelper.h"
#include "output/universal_output_manager.h"
#include "output/universal_feedback_manager.h"
#include "input/universal_human_interface_manager.h"
#include "pico/unique_id.h"
#include "pico/time.h"

namespace {

static char serialString[17] = "0000000000000000";
static uint64_t lastReportSentUs[OAG_MULTI_HID_SLOT_COUNT] {};
static constexpr uint64_t OAG_HID_KEEPALIVE_US = 250000;

static uint32_t standardButtonsForState(GamepadState const& state) {
    uint32_t buttons = 0;

    if (state.buttons & GAMEPAD_MASK_B1) buttons |= 1UL << 0;  // A / South
    if (state.buttons & GAMEPAD_MASK_B2) buttons |= 1UL << 1;  // B / East
    if (state.buttons & GAMEPAD_MASK_B3) buttons |= 1UL << 2;  // X / West
    if (state.buttons & GAMEPAD_MASK_B4) buttons |= 1UL << 3;  // Y / North
    if (state.buttons & GAMEPAD_MASK_L1) buttons |= 1UL << 4;
    if (state.buttons & GAMEPAD_MASK_R1) buttons |= 1UL << 5;

    if ((state.buttons & GAMEPAD_MASK_L2) || state.lt != 0) buttons |= 1UL << 6;
    if ((state.buttons & GAMEPAD_MASK_R2) || state.rt != 0) buttons |= 1UL << 7;

    if (state.buttons & GAMEPAD_MASK_S1) buttons |= 1UL << 8;  // Back / View
    if (state.buttons & GAMEPAD_MASK_S2) buttons |= 1UL << 9;  // Start / Menu
    if (state.buttons & GAMEPAD_MASK_L3) buttons |= 1UL << 10;
    if (state.buttons & GAMEPAD_MASK_R3) buttons |= 1UL << 11;

    // D-pad is intentionally emitted only through the HID Hat Switch below.
    // Do not duplicate it into Button usages: generic-HID consumers can assign
    // those raw button indexes differently, which caused D-pad Up to appear as
    // Home/Guide on the user's Windows/browser path.
    if (state.buttons & GAMEPAD_MASK_A1) buttons |= 1UL << 16; // Home / Guide
    if (state.buttons & GAMEPAD_MASK_A2) buttons |= 1UL << 17; // Capture / Touchpad
    if (state.buttons & GAMEPAD_MASK_A3) buttons |= 1UL << 18;
    if (state.buttons & GAMEPAD_MASK_A4) buttons |= 1UL << 19;

    // E1..E12 already occupy bits 20..31 and do not collide with the
    // standard browser/gamepad positions above.
    buttons |= state.buttons & 0xFFF00000UL;

    return buttons;
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
    memset(lastReportSentUs, 0, sizeof(lastReportSentUs));
    memset(lastMouseGeneration, 0, sizeof(lastMouseGeneration));
    lastKeyboardReport = OAGKeyboardReport {};
    lastConsumerReport = OAGConsumerReport {};
    lastMouseReport = OAGMouseReport {};
    lastKeyboardReportValid = false;
    lastConsumerReportValid = false;
    lastMouseReportValid = false;
    pendingMouseX = 0;
    pendingMouseY = 0;
    pendingMouseWheel = 0;
    pendingMousePan = 0;

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

    // Project canonical controller buttons to a stable generic-HID order.
    // D-pad is not duplicated here; it is represented exclusively by Hat.
    report.buttons = standardButtonsForState(state);
    report.hat = dpadToHat(state.dpad);

    report.lx = static_cast<uint8_t>(state.lx >> 8);
    report.ly = static_cast<uint8_t>(state.ly >> 8);
    report.rx = static_cast<uint8_t>(state.rx >> 8);
    report.ry = static_cast<uint8_t>(state.ry >> 8);

    return report;
}

bool OAGMultiHIDDriver::process(Gamepad* gamepad) {
    (void)gamepad;

    bool anySent = false;
    const uint64_t nowUs = time_us_64();

    for (uint8_t slot = 0; slot < OAG_MULTI_HID_SLOT_COUNT; slot++) {
        UniversalOutputSlotSnapshot output {};

        const bool hasOutput =
            UOUTPUT.snapshot(slot, output) &&
            output.connected &&
            output.hasReport;

        GamepadState neutral {};
        const GamepadState& state =
            hasOutput ? output.state : neutral;

        reports[slot] = buildReport(state);

        const bool changed =
            !lastReportValid[slot] ||
            memcmp(
                &lastReports[slot],
                &reports[slot],
                sizeof(OAGMultiHIDReport)
            ) != 0;

        const bool keepaliveDue =
            !lastReportValid[slot] ||
            nowUs - lastReportSentUs[slot] >= OAG_HID_KEEPALIVE_US;

        if (
            (changed || keepaliveDue) &&
            tud_ready() &&
            tud_hid_n_ready(slot) &&
            tud_hid_n_report(
                slot,
                0,
                &reports[slot],
                sizeof(OAGMultiHIDReport)
            )
        ) {
            lastReports[slot] = reports[slot];
            lastReportValid[slot] = true;
            lastReportSentUs[slot] = nowUs;
            anySent = true;
        }
    }

    processHumanInterfaces(anySent);
    return anySent;
}

namespace {
static int16_t clampI16(int32_t value) {
    if (value < -32767) return -32767;
    if (value > 32767) return 32767;
    return static_cast<int16_t>(value);
}

static int8_t clampI8(int32_t value) {
    if (value < -127) return -127;
    if (value > 127) return 127;
    return static_cast<int8_t>(value);
}

static int consumerUsageBit(uint16_t usage) {
    switch (usage) {
        case 0x00E2: return 0;  // Mute
        case 0x00E9: return 1;  // Volume Up
        case 0x00EA: return 2;  // Volume Down
        case 0x00CD: return 3;  // Play/Pause
        case 0x00B5: return 4;  // Next
        case 0x00B6: return 5;  // Previous
        case 0x00B7: return 6;  // Stop
        case 0x00B8: return 7;  // Eject
        case 0x0223: return 8;  // Home
        case 0x0221: return 9;  // Search
        case 0x0224: return 10; // Back
        case 0x0225: return 11; // Forward
        case 0x0227: return 12; // Refresh
        case 0x022A: return 13; // Bookmarks
        case 0x00B3: return 14; // Fast Forward
        case 0x00B4: return 15; // Rewind
        default: return -1;
    }
}
} // namespace

OAGKeyboardReport OAGMultiHIDDriver::buildKeyboardReport() const {
    OAGKeyboardReport report {};

    for (uint8_t slot = 0; slot < UNIVERSAL_HID_SLOT_COUNT; slot++) {
        UniversalKeyboardSlotSnapshot snapshot {};
        if (
            !UHIDINPUT.snapshotKeyboard(slot, snapshot) ||
            !snapshot.connected ||
            !snapshot.hasReport
        ) {
            continue;
        }

        for (uint8_t word = 0; word < 8; word++) {
            report.keys[word] |= snapshot.state.keys[word];
        }

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (snapshot.state.modifiers & (1u << bit)) {
                const uint8_t usage = static_cast<uint8_t>(0xE0 + bit);
                report.keys[usage >> 5] |=
                    1u << static_cast<uint8_t>(usage & 31);
            }
        }
    }

    return report;
}

OAGConsumerReport OAGMultiHIDDriver::buildConsumerReport() const {
    OAGConsumerReport report {};

    for (uint8_t slot = 0; slot < UNIVERSAL_HID_SLOT_COUNT; slot++) {
        UniversalKeyboardSlotSnapshot snapshot {};
        if (
            !UHIDINPUT.snapshotKeyboard(slot, snapshot) ||
            !snapshot.connected ||
            !snapshot.hasReport
        ) {
            continue;
        }

        for (
            uint8_t i = 0;
            i < snapshot.state.consumerUsageCount &&
            i < UNIVERSAL_KEYBOARD_MAX_CONSUMER_USAGES;
            i++
        ) {
            const int bit = consumerUsageBit(
                snapshot.state.consumerUsages[i]
            );
            if (bit >= 0) {
                report.usages |= static_cast<uint16_t>(1u << bit);
            }
        }
    }

    return report;
}

OAGMouseReport OAGMultiHIDDriver::buildMouseReport() {
    OAGMouseReport report {};
    uint8_t buttons = 0;

    for (uint8_t slot = 0; slot < UNIVERSAL_HID_SLOT_COUNT; slot++) {
        UniversalMouseSlotSnapshot snapshot {};
        if (!UHIDINPUT.snapshotMouse(slot, snapshot)) {
            continue;
        }

        if (!snapshot.connected) {
            lastMouseGeneration[slot] = snapshot.generation;
            continue;
        }

        if (snapshot.hasReport) {
            buttons |= static_cast<uint8_t>(snapshot.state.buttons & 0xFFu);

            if (snapshot.generation != lastMouseGeneration[slot]) {
                pendingMouseX += snapshot.state.x;
                pendingMouseY += snapshot.state.y;
                pendingMouseWheel += snapshot.state.wheel;
                pendingMousePan += snapshot.state.horizontalWheel;
                lastMouseGeneration[slot] = snapshot.generation;
            }
        }
    }

    report.buttons = buttons;
    report.x = clampI16(pendingMouseX);
    report.y = clampI16(pendingMouseY);
    report.wheel = clampI8(pendingMouseWheel);
    report.pan = clampI8(pendingMousePan);
    return report;
}

void OAGMultiHIDDriver::processHumanInterfaces(bool& anySent) {
    if (!tud_ready()) {
        return;
    }

    const OAGKeyboardReport keyboard = buildKeyboardReport();
    if (
        (!lastKeyboardReportValid ||
         memcmp(&keyboard, &lastKeyboardReport, sizeof(keyboard)) != 0) &&
        tud_hid_n_ready(OAG_MULTI_HID_KEYBOARD_INTERFACE) &&
        tud_hid_n_report(
            OAG_MULTI_HID_KEYBOARD_INTERFACE,
            OAG_HID_REPORT_ID_KEYBOARD,
            &keyboard,
            sizeof(keyboard)
        )
    ) {
        lastKeyboardReport = keyboard;
        lastKeyboardReportValid = true;
        anySent = true;
    }

    const OAGConsumerReport consumer = buildConsumerReport();
    if (
        (!lastConsumerReportValid ||
         memcmp(&consumer, &lastConsumerReport, sizeof(consumer)) != 0) &&
        tud_hid_n_ready(OAG_MULTI_HID_KEYBOARD_INTERFACE) &&
        tud_hid_n_report(
            OAG_MULTI_HID_KEYBOARD_INTERFACE,
            OAG_HID_REPORT_ID_CONSUMER,
            &consumer,
            sizeof(consumer)
        )
    ) {
        lastConsumerReport = consumer;
        lastConsumerReportValid = true;
        anySent = true;
    }

    const OAGMouseReport mouse = buildMouseReport();
    const bool mouseChanged =
        !lastMouseReportValid ||
        mouse.buttons != lastMouseReport.buttons ||
        mouse.x != 0 ||
        mouse.y != 0 ||
        mouse.wheel != 0 ||
        mouse.pan != 0;

    if (
        mouseChanged &&
        tud_hid_n_ready(OAG_MULTI_HID_MOUSE_INTERFACE) &&
        tud_hid_n_report(
            OAG_MULTI_HID_MOUSE_INTERFACE,
            OAG_HID_REPORT_ID_MOUSE,
            &mouse,
            sizeof(mouse)
        )
    ) {
        pendingMouseX -= mouse.x;
        pendingMouseY -= mouse.y;
        pendingMouseWheel -= mouse.wheel;
        pendingMousePan -= mouse.pan;
        lastMouseReport = mouse;
        lastMouseReport.x = 0;
        lastMouseReport.y = 0;
        lastMouseReport.wheel = 0;
        lastMouseReport.pan = 0;
        lastMouseReportValid = true;
        anySent = true;
    }
}

void OAGMultiHIDDriver::set_report_with_itf(
    uint8_t itf,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const* buffer,
    uint16_t bufsize
) {
    if (
        itf == OAG_MULTI_HID_KEYBOARD_INTERFACE &&
        report_type == HID_REPORT_TYPE_OUTPUT &&
        report_id == OAG_HID_REPORT_ID_KEYBOARD &&
        buffer != nullptr &&
        bufsize >= 1
    ) {
        UHIDINPUT.publishKeyboardLedState(buffer[0]);
        return;
    }

    if (
        itf >= OAG_MULTI_HID_SLOT_COUNT ||
        report_type != HID_REPORT_TYPE_OUTPUT ||
        buffer == nullptr ||
        bufsize < 2
    ) {
        return;
    }

    UFEEDBACK.publish(
        itf,
        buffer[0],
        buffer[1],
        bufsize > 2 ? buffer[2] : 0,
        bufsize > 3 ? buffer[3] : 0
    );
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

    // Multi-interface callers must use get_report_with_itf() so the
    // requested logical controller is unambiguous.
    return 0;
}

uint16_t OAGMultiHIDDriver::get_report_with_itf(
    uint8_t itf,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t* buffer,
    uint16_t reqlen
) {
    (void)report_id;

    if (
        report_type != HID_REPORT_TYPE_INPUT ||
        buffer == nullptr ||
        reqlen == 0
    ) {
        return 0;
    }

    if (itf < OAG_MULTI_HID_SLOT_COUNT) {
        UniversalOutputSlotSnapshot output {};
        const bool hasOutput =
            UOUTPUT.snapshot(itf, output) &&
            output.connected &&
            output.hasReport;

        GamepadState neutral {};
        reports[itf] = buildReport(
            hasOutput ? output.state : neutral
        );

        const uint16_t reportSize =
            static_cast<uint16_t>(sizeof(OAGMultiHIDReport));
        const uint16_t copyLength =
            reqlen < reportSize ? reqlen : reportSize;

        memcpy(buffer, &reports[itf], copyLength);
        return copyLength;
    }

    if (itf == OAG_MULTI_HID_KEYBOARD_INTERFACE) {
        if (report_id == OAG_HID_REPORT_ID_KEYBOARD) {
            const OAGKeyboardReport report = buildKeyboardReport();
            const uint16_t n = reqlen < sizeof(report)
                ? reqlen
                : static_cast<uint16_t>(sizeof(report));
            memcpy(buffer, &report, n);
            return n;
        }

        if (report_id == OAG_HID_REPORT_ID_CONSUMER) {
            const OAGConsumerReport report = buildConsumerReport();
            const uint16_t n = reqlen < sizeof(report)
                ? reqlen
                : static_cast<uint16_t>(sizeof(report));
            memcpy(buffer, &report, n);
            return n;
        }
    }

    if (
        itf == OAG_MULTI_HID_MOUSE_INTERFACE &&
        report_id == OAG_HID_REPORT_ID_MOUSE
    ) {
        OAGMouseReport report = lastMouseReport;
        report.x = 0;
        report.y = 0;
        report.wheel = 0;
        report.pan = 0;
        const uint16_t n = reqlen < sizeof(report)
            ? reqlen
            : static_cast<uint16_t>(sizeof(report));
        memcpy(buffer, &report, n);
        return n;
    }

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

    return nullptr;
}

const uint8_t* OAGMultiHIDDriver::get_descriptor_device_cb() {
    return oag_multi_hid_device_descriptor;
}

const uint8_t* OAGMultiHIDDriver::get_hid_descriptor_report_cb(
    uint8_t itf
) {
    if (itf < OAG_MULTI_HID_SLOT_COUNT) {
        return oag_multi_hid_report_descriptor;
    }

    if (itf == OAG_MULTI_HID_KEYBOARD_INTERFACE) {
        return oag_keyboard_report_descriptor;
    }

    if (itf == OAG_MULTI_HID_MOUSE_INTERFACE) {
        return oag_mouse_report_descriptor;
    }

    return nullptr;
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
        OAG_MULTI_HID_INTERFACE_COUNT,
        0x01,
        0x00,
        0x80,
        0x32
    };

    memcpy(out, configHeader, sizeof(configHeader));
    out += sizeof(configHeader);

    for (
        uint8_t itf = 0;
        itf < OAG_MULTI_HID_INTERFACE_COUNT;
        itf++
    ) {
        const uint8_t epIn =
            static_cast<uint8_t>(0x80 | (itf + 1));

        uint16_t reportDescriptorLength = 0;
        if (itf < OAG_MULTI_HID_SLOT_COUNT) {
            reportDescriptorLength =
                sizeof(oag_multi_hid_report_descriptor);
        } else if (itf == OAG_MULTI_HID_KEYBOARD_INTERFACE) {
            reportDescriptorLength =
                sizeof(oag_keyboard_report_descriptor);
        } else if (itf == OAG_MULTI_HID_MOUSE_INTERFACE) {
            reportDescriptorLength =
                sizeof(oag_mouse_report_descriptor);
        }

        const uint8_t interfaceBlock[] = {
            // Interface
            0x09, 0x04,
            itf,
            0x00,
            0x01,
            0x03,
            0x00,
            0x00,
            0x00,

            // HID descriptor
            0x09, 0x21,
            0x11, 0x01,
            0x00,
            0x01,
            0x22,
            static_cast<uint8_t>(reportDescriptorLength & 0xFF),
            static_cast<uint8_t>(
                (reportDescriptorLength >> 8) & 0xFF
            ),

            // Interrupt IN endpoint, 1 ms polling.
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
