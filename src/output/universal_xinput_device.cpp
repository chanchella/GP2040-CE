#include "output/universal_xinput_device.h"

#include <cstring>
#include <climits>

#include "output/universal_output_manager.h"
#include "device/usbd_pvt.h"
#include "tusb.h"

// G2C1 PC multi-controller descriptor.
//
// One physical OAG Vortex device exposes four XUSB-style gamepad interfaces.
// Every interface has its own interrupt IN/OUT pair.
//
// Interface 0: IN 0x81 / OUT 0x02
// Interface 1: IN 0x83 / OUT 0x04
// Interface 2: IN 0x85 / OUT 0x06
// Interface 3: IN 0x87 / OUT 0x08
//
// This deliberately omits the legacy audio/plugin/security interfaces.
// Console authentication profiles remain a separate later platform layer.
static const uint8_t universal_xinput_configuration_descriptor[] = {
    0x09, 0x02,
    0xA9, 0x00, // 169 bytes total
    0x04,       // four gamepad interfaces
    0x01,
    0x00,
    0xA0,       // remote wakeup
    0xFA,

    // -------- Controller / Output Slot 0 --------
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25,
    0x81, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x13,
    0x02, 0x08,
    0x00, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x08,

    // -------- Controller / Output Slot 1 --------
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25,
    0x83, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x13,
    0x04, 0x08,
    0x00, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x08,

    // -------- Controller / Output Slot 2 --------
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25,
    0x85, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x13,
    0x06, 0x08,
    0x00, 0x00,
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x06, 0x03, 0x20, 0x00, 0x08,

    // -------- Controller / Output Slot 3 --------
    0x09, 0x04, 0x03, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25,
    0x87, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x13,
    0x08, 0x08,
    0x00, 0x00,
    0x07, 0x05, 0x87, 0x03, 0x20, 0x00, 0x01,
    0x07, 0x05, 0x08, 0x03, 0x20, 0x00, 0x08,
};

static_assert(
    sizeof(universal_xinput_configuration_descriptor) == 0xA9,
    "G2C1 XInput configuration descriptor size mismatch"
);

UniversalXInputDevice& UniversalXInputDevice::getInstance() {
    static UniversalXInputDevice instance;
    return instance;
}

UniversalXInputDevice::UniversalXInputDevice() {
    reset();
}

void UniversalXInputDevice::initializeClassDriver(
    usbd_class_driver_t& driver
) {
    reset();

    driver = {
#if CFG_TUSB_DEBUG >= 2
        .name = "OAG_MULTI_XINPUT",
#endif
        .init = classInit,
        .reset = classReset,
        .open = classOpen,
        .control_xfer_cb = classControlXfer,
        .xfer_cb = classXfer,
        .sof = nullptr,
    };
}

const uint8_t* UniversalXInputDevice::configurationDescriptor() const {
    return universal_xinput_configuration_descriptor;
}

void UniversalXInputDevice::classInit() {
    UXINPUT_DEVICE.reset();
}

void UniversalXInputDevice::classReset(uint8_t rhport) {
    (void)rhport;
    UXINPUT_DEVICE.reset();
}

uint16_t UniversalXInputDevice::classOpen(
    uint8_t rhport,
    tusb_desc_interface_t const* itfDescriptor,
    uint16_t maxLength
) {
    return UXINPUT_DEVICE.open(rhport, itfDescriptor, maxLength);
}

bool UniversalXInputDevice::classControlXfer(
    uint8_t rhport,
    uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;

    // G2C1 has no console-auth control plane.
    return true;
}

bool UniversalXInputDevice::classXfer(
    uint8_t rhport,
    uint8_t epAddr,
    xfer_result_t result,
    uint32_t xferredBytes
) {
    return UXINPUT_DEVICE.xfer(
        rhport,
        epAddr,
        result,
        xferredBytes
    );
}

void UniversalXInputDevice::reset() {
    memset(endpointIn, 0, sizeof(endpointIn));
    memset(endpointOut, 0, sizeof(endpointOut));
    memset(txReport, 0, sizeof(txReport));
    memset(lastReport, 0, sizeof(lastReport));
    memset(lastReportValid, 0, sizeof(lastReportValid));
    memset(outTransferBuffer, 0, sizeof(outTransferBuffer));
    memset(lastOutReport, 0, sizeof(lastOutReport));
    memset(outReportPending, 0, sizeof(outReportPending));
}

uint16_t UniversalXInputDevice::open(
    uint8_t rhport,
    tusb_desc_interface_t const* itfDescriptor,
    uint16_t maxLength
) {
    TU_VERIFY(itfDescriptor != nullptr, 0);

    if (
        itfDescriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        itfDescriptor->bInterfaceSubClass != 0x5D ||
        itfDescriptor->bInterfaceProtocol != 0x01 ||
        itfDescriptor->bNumEndpoints != 2
    ) {
        return 0;
    }

    const uint8_t slot = itfDescriptor->bInterfaceNumber;
    TU_VERIFY(slot < UNIVERSAL_XINPUT_DEVICE_COUNT, 0);

    uint16_t driverLength = sizeof(tusb_desc_interface_t);
    TU_VERIFY(maxLength >= driverLength, 0);

    uint8_t const* cursor =
        reinterpret_cast<uint8_t const*>(itfDescriptor);

    cursor = tu_desc_next(cursor);

    TU_VERIFY(
        maxLength >= driverLength + tu_desc_len(cursor),
        0
    );
    TU_VERIFY(tu_desc_type(cursor) == 0x21, 0);

    driverLength += tu_desc_len(cursor);
    cursor = tu_desc_next(cursor);

    const uint16_t endpointBytes =
        itfDescriptor->bNumEndpoints * sizeof(tusb_desc_endpoint_t);

    TU_VERIFY(
        maxLength >= driverLength + endpointBytes,
        0
    );

    uint8_t epOut = 0;
    uint8_t epIn = 0;

    TU_VERIFY(
        usbd_open_edpt_pair(
            rhport,
            cursor,
            itfDescriptor->bNumEndpoints,
            TUSB_XFER_INTERRUPT,
            &epOut,
            &epIn
        ),
        0
    );

    TU_VERIFY(epIn != 0 && epOut != 0, 0);

    endpointIn[slot] = epIn;
    endpointOut[slot] = epOut;

    driverLength += endpointBytes;
    return driverLength;
}

int8_t UniversalXInputDevice::slotForOutEndpoint(
    uint8_t epAddr
) const {
    for (uint8_t slot = 0; slot < UNIVERSAL_XINPUT_DEVICE_COUNT; slot++) {
        if (endpointOut[slot] == epAddr) {
            return static_cast<int8_t>(slot);
        }
    }

    return -1;
}

bool UniversalXInputDevice::xfer(
    uint8_t rhport,
    uint8_t epAddr,
    xfer_result_t result,
    uint32_t xferredBytes
) {
    const int8_t slotIndex = slotForOutEndpoint(epAddr);

    if (slotIndex >= 0) {
        const uint8_t slot = static_cast<uint8_t>(slotIndex);

        if (result == XFER_RESULT_SUCCESS) {
            const uint32_t copyLength =
                xferredBytes < XINPUT_OUT_SIZE
                    ? xferredBytes
                    : XINPUT_OUT_SIZE;

            memset(lastOutReport[slot], 0, XINPUT_OUT_SIZE);
            memcpy(
                lastOutReport[slot],
                outTransferBuffer[slot],
                copyLength
            );
            outReportPending[slot] = true;
        }

        // Keep every controller's OUT endpoint continuously armed.
        usbd_edpt_xfer(
            rhport,
            endpointOut[slot],
            outTransferBuffer[slot],
            XINPUT_OUT_SIZE
        );
    }

    return true;
}

XInputReport UniversalXInputDevice::makeNeutralReport() {
    XInputReport report {};
    report.report_id = 0;
    report.report_size = XINPUT_ENDPOINT_SIZE;
    report.buttons1 = 0;
    report.buttons2 = 0;
    report.lt = 0;
    report.rt = 0;
    report.lx = 0;
    report.ly = 0;
    report.rx = 0;
    report.ry = 0;

    return report;
}

XInputReport UniversalXInputDevice::makeReport(
    GamepadState const& state
) {
    XInputReport report = makeNeutralReport();

    report.buttons1 = 0
        | ((state.dpad & GAMEPAD_MASK_UP)    ? XBOX_MASK_UP    : 0)
        | ((state.dpad & GAMEPAD_MASK_DOWN)  ? XBOX_MASK_DOWN  : 0)
        | ((state.dpad & GAMEPAD_MASK_LEFT)  ? XBOX_MASK_LEFT  : 0)
        | ((state.dpad & GAMEPAD_MASK_RIGHT) ? XBOX_MASK_RIGHT : 0)
        | ((state.buttons & GAMEPAD_MASK_S2)  ? XBOX_MASK_START : 0)
        | ((state.buttons & GAMEPAD_MASK_S1)  ? XBOX_MASK_BACK  : 0)
        | ((state.buttons & GAMEPAD_MASK_L3)  ? XBOX_MASK_LS    : 0)
        | ((state.buttons & GAMEPAD_MASK_R3)  ? XBOX_MASK_RS    : 0);

    report.buttons2 = 0
        | ((state.buttons & GAMEPAD_MASK_L1) ? XBOX_MASK_LB   : 0)
        | ((state.buttons & GAMEPAD_MASK_R1) ? XBOX_MASK_RB   : 0)
        | ((state.buttons & GAMEPAD_MASK_A1) ? XBOX_MASK_HOME : 0)
        | ((state.buttons & GAMEPAD_MASK_B1) ? XBOX_MASK_A    : 0)
        | ((state.buttons & GAMEPAD_MASK_B2) ? XBOX_MASK_B    : 0)
        | ((state.buttons & GAMEPAD_MASK_B3) ? XBOX_MASK_X    : 0)
        | ((state.buttons & GAMEPAD_MASK_B4) ? XBOX_MASK_Y    : 0);

    report.lt =
        state.lt != 0
            ? state.lt
            : ((state.buttons & GAMEPAD_MASK_L2) ? 0xFF : 0);

    report.rt =
        state.rt != 0
            ? state.rt
            : ((state.buttons & GAMEPAD_MASK_R2) ? 0xFF : 0);

    report.lx =
        static_cast<int16_t>(state.lx) + INT16_MIN;

    report.ly =
        static_cast<int16_t>(~state.ly) + INT16_MIN;

    report.rx =
        static_cast<int16_t>(state.rx) + INT16_MIN;

    report.ry =
        static_cast<int16_t>(~state.ry) + INT16_MIN;

    return report;
}

bool UniversalXInputDevice::process() {
    bool anyReportSent = false;

    for (uint8_t slot = 0; slot < UNIVERSAL_XINPUT_DEVICE_COUNT; slot++) {
        // Keep OUT endpoints ready for rumble / player LED traffic.
        if (
            tud_ready() &&
            endpointOut[slot] != 0 &&
            !usbd_edpt_busy(0, endpointOut[slot])
        ) {
            usbd_edpt_claim(0, endpointOut[slot]);

            if (!usbd_edpt_xfer(
                    0,
                    endpointOut[slot],
                    outTransferBuffer[slot],
                    XINPUT_OUT_SIZE
                )) {
                usbd_edpt_release(0, endpointOut[slot]);
            }
        }

        UniversalOutputSlotSnapshot output {};
        const bool hasSnapshot = UOUTPUT.snapshot(slot, output);

        const XInputReport desired =
            hasSnapshot && output.connected && output.hasReport
                ? makeReport(output.state)
                : makeNeutralReport();

        const bool changed =
            !lastReportValid[slot] ||
            memcmp(
                &lastReport[slot],
                &desired,
                sizeof(XInputReport)
            ) != 0;

        if (!changed) {
            continue;
        }

        if (
            !tud_ready() ||
            endpointIn[slot] == 0 ||
            usbd_edpt_busy(0, endpointIn[slot])
        ) {
            continue;
        }

        memcpy(
            &txReport[slot],
            &desired,
            sizeof(XInputReport)
        );

        if (!usbd_edpt_claim(0, endpointIn[slot])) {
            continue;
        }

        if (!usbd_edpt_xfer(
                0,
                endpointIn[slot],
                reinterpret_cast<uint8_t*>(&txReport[slot]),
                sizeof(XInputReport)
            )) {
            usbd_edpt_release(0, endpointIn[slot]);
            continue;
        }

        memcpy(
            &lastReport[slot],
            &desired,
            sizeof(XInputReport)
        );

        lastReportValid[slot] = true;
        anyReportSent = true;
    }

    return anyReportSent;
}
