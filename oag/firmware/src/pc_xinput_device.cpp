#include "oag/firmware/pc_xinput_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "oag/output/xinput/xinput_feedback_decoder.h"

namespace {

constexpr std::uint8_t kDeviceRhPort = 0;
constexpr std::uint16_t kOutBufferSize = 32;
constexpr std::uint8_t kReservedDescriptorType = 0x21;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcXinputDevice::kOutputSlots;

struct XinputOutputRuntime {
    std::uint8_t interfaceNumber = 0xFF;
    std::uint8_t endpointIn = 0;
    std::uint8_t endpointOut = 0;
    std::uint8_t outBuffer[kOutBufferSize] {};
    oag::RumbleCommand latestRumble {};
    bool rumblePending = false;
};

std::array<XinputOutputRuntime, kOutputSlots> gSlots {};
oag::XinputFeedbackDecoder gFeedbackDecoder;

void resetRuntime() {
    gSlots = {};
    for (auto& slot : gSlots) {
        slot.interfaceNumber = 0xFF;
    }
}

void driverInit() {
    resetRuntime();
}

bool driverDeinit() {
    resetRuntime();
    return true;
}

void driverReset(std::uint8_t rhport) {
    (void)rhport;
    resetRuntime();
}

int slotForInterface(std::uint8_t interfaceNumber) {
    if (interfaceNumber >= kOutputSlots) {
        return -1;
    }
    return static_cast<int>(interfaceNumber);
}

int slotForOutEndpoint(std::uint8_t endpointAddress) {
    for (std::size_t i = 0; i < gSlots.size(); ++i) {
        if (gSlots[i].endpointOut != 0 &&
            gSlots[i].endpointOut == endpointAddress) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

std::uint16_t driverOpen(
    std::uint8_t rhport,
    tusb_desc_interface_t const* interfaceDescriptor,
    std::uint16_t maxLength
) {
    if (interfaceDescriptor == nullptr ||
        interfaceDescriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        interfaceDescriptor->bInterfaceSubClass != 0x5D ||
        interfaceDescriptor->bInterfaceProtocol != 0x01 ||
        interfaceDescriptor->bNumEndpoints != 2) {
        return 0;
    }

    const int slotIndex =
        slotForInterface(interfaceDescriptor->bInterfaceNumber);

    if (slotIndex < 0) {
        return 0;
    }

    auto& runtime = gSlots[static_cast<std::size_t>(slotIndex)];

    std::uint16_t driverLength =
        static_cast<std::uint16_t>(
            sizeof(tusb_desc_interface_t) +
            interfaceDescriptor->bNumEndpoints *
                sizeof(tusb_desc_endpoint_t)
        );

    if (maxLength < driverLength) {
        return 0;
    }

    const std::uint8_t* cursor =
        reinterpret_cast<const std::uint8_t*>(interfaceDescriptor);

    cursor = tu_desc_next(cursor);

    if (tu_desc_type(cursor) != kReservedDescriptorType) {
        return 0;
    }

    const std::uint8_t customLength = tu_desc_len(cursor);
    if (customLength == 0 ||
        driverLength + customLength > maxLength) {
        return 0;
    }

    driverLength = static_cast<std::uint16_t>(
        driverLength + customLength
    );

    cursor = tu_desc_next(cursor);

    std::uint8_t epOut = 0;
    std::uint8_t epIn = 0;

    if (!usbd_open_edpt_pair(
            rhport,
            cursor,
            interfaceDescriptor->bNumEndpoints,
            TUSB_XFER_INTERRUPT,
            &epOut,
            &epIn
        )) {
        return 0;
    }

    if (epIn == 0 || epOut == 0) {
        return 0;
    }

    runtime = {};
    runtime.interfaceNumber = interfaceDescriptor->bInterfaceNumber;
    runtime.endpointIn = epIn;
    runtime.endpointOut = epOut;

    return driverLength;
}

bool driverControlXfer(
    std::uint8_t rhport,
    std::uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;

    // PC XInput compatibility does not require console authentication.
    return true;
}

bool driverXfer(
    std::uint8_t rhport,
    std::uint8_t epAddr,
    xfer_result_t result,
    std::uint32_t xferredBytes
) {
    const int slotIndex = slotForOutEndpoint(epAddr);
    if (slotIndex < 0) {
        return true;
    }

    auto& runtime = gSlots[static_cast<std::size_t>(slotIndex)];

    if (result == XFER_RESULT_SUCCESS) {
        oag::RumbleCommand decoded = runtime.latestRumble;

        if (gFeedbackDecoder.decodeRumble(
                runtime.outBuffer,
                static_cast<std::size_t>(xferredBytes),
                decoded
            )) {
            runtime.latestRumble = decoded;
            runtime.rumblePending = true;
        }
    }

    if (runtime.endpointOut != 0) {
        usbd_edpt_xfer(
            rhport,
            runtime.endpointOut,
            runtime.outBuffer,
            sizeof(runtime.outBuffer)
        );
    }

    return true;
}

const usbd_class_driver_t kDriver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "OAG_MULTI_XINPUT_DEVICE",
#else
    .name = nullptr,
#endif
    .init = driverInit,
    .deinit = driverDeinit,
    .reset = driverReset,
    .open = driverOpen,
    .control_xfer_cb = driverControlXfer,
    .xfer_cb = driverXfer,
    .sof = nullptr,
};

bool armOutputIfIdle(std::size_t slotIndex) {
    if (slotIndex >= gSlots.size()) {
        return false;
    }

    auto& runtime = gSlots[slotIndex];

    if (!tud_ready() ||
        runtime.endpointOut == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, runtime.endpointOut)) {
        return false;
    }

    if (!usbd_edpt_claim(kDeviceRhPort, runtime.endpointOut)) {
        return false;
    }

    const bool queued = usbd_edpt_xfer(
        kDeviceRhPort,
        runtime.endpointOut,
        runtime.outBuffer,
        sizeof(runtime.outBuffer)
    );

    if (!queued) {
        usbd_edpt_release(kDeviceRhPort, runtime.endpointOut);
        return false;
    }

    usbd_edpt_release(kDeviceRhPort, runtime.endpointOut);
    return true;
}

} // namespace

extern "C" usbd_class_driver_t const* usbd_app_driver_get_cb(
    std::uint8_t* driverCount
) {
    *driverCount = 1;
    return &kDriver;
}

extern "C" bool tud_vendor_control_xfer_cb(
    std::uint8_t rhport,
    std::uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;

    return false;
}

namespace oag::firmware {

void PcXinputDevice::task() {
    for (std::size_t slot = 0; slot < kOutputSlots; ++slot) {
        armOutputIfIdle(slot);
    }
}

bool PcXinputDevice::send(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    if (logicalSlot >= kOutputSlots) {
        return false;
    }

    auto& runtime = gSlots[logicalSlot];

    if (!tud_ready() ||
        runtime.endpointIn == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, runtime.endpointIn)) {
        return false;
    }

    const XinputReport next =
        encoders_[logicalSlot].encode(state);

    if (!usbd_edpt_claim(kDeviceRhPort, runtime.endpointIn)) {
        return false;
    }

    reports_[logicalSlot] = next;

    const bool queued = usbd_edpt_xfer(
        kDeviceRhPort,
        runtime.endpointIn,
        reinterpret_cast<std::uint8_t*>(&reports_[logicalSlot]),
        sizeof(reports_[logicalSlot])
    );

    if (!queued) {
        usbd_edpt_release(kDeviceRhPort, runtime.endpointIn);
        return false;
    }

    usbd_edpt_release(kDeviceRhPort, runtime.endpointIn);
    return true;
}

bool PcXinputDevice::sendNeutral(std::uint8_t logicalSlot) {
    LogicalGamepadState neutral {};
    neutral.connected = true;
    return send(logicalSlot, neutral);
}

bool PcXinputDevice::takeRumble(
    std::uint8_t& logicalSlot,
    RumbleCommand& output
) {
    for (std::size_t offset = 0; offset < kOutputSlots; ++offset) {
        const std::size_t slot =
            (rumbleScanStart_ + offset) % kOutputSlots;

        if (!gSlots[slot].rumblePending) {
            continue;
        }

        logicalSlot = static_cast<std::uint8_t>(slot);
        output = gSlots[slot].latestRumble;
        gSlots[slot].rumblePending = false;
        rumbleScanStart_ = (slot + 1u) % kOutputSlots;
        return true;
    }

    return false;
}

} // namespace oag::firmware
