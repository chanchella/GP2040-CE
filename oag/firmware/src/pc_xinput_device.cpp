#include "oag/firmware/pc_xinput_device.h"

#include <cstdint>
#include <cstring>

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "oag/output/xinput/xinput_feedback_decoder.h"

namespace {

constexpr std::uint8_t kDeviceRhPort = 0;
constexpr std::uint16_t kOutBufferSize = 32;
constexpr std::uint8_t kReservedDescriptorType = 0x21;
constexpr std::uint8_t kSecurityDescriptorType = 0x41;

std::uint8_t gEndpointIn = 0;
std::uint8_t gEndpointOut = 0;
std::uint8_t gOutBuffer[kOutBufferSize] {};

oag::XinputFeedbackDecoder gFeedbackDecoder;
oag::RumbleCommand gLatestRumble {};
bool gRumblePending = false;

void resetRuntime() {
    gEndpointIn = 0;
    gEndpointOut = 0;
    std::memset(gOutBuffer, 0, sizeof(gOutBuffer));
    gLatestRumble = {};
    gRumblePending = false;
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

std::uint16_t driverOpen(
    std::uint8_t rhport,
    tusb_desc_interface_t const* interfaceDescriptor,
    std::uint16_t maxLength
) {
    if (interfaceDescriptor == nullptr ||
        interfaceDescriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) {
        return 0;
    }

    std::uint16_t driverLength =
        static_cast<std::uint16_t>(
            sizeof(tusb_desc_interface_t) +
            interfaceDescriptor->bNumEndpoints * sizeof(tusb_desc_endpoint_t)
        );

    if (maxLength < driverLength) {
        return 0;
    }

    const std::uint8_t* cursor =
        reinterpret_cast<const std::uint8_t*>(interfaceDescriptor);

    if (interfaceDescriptor->bInterfaceSubClass == 0x5D &&
        (interfaceDescriptor->bInterfaceProtocol == 0x01 ||
         interfaceDescriptor->bInterfaceProtocol == 0x02 ||
         interfaceDescriptor->bInterfaceProtocol == 0x03)) {

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

        if (interfaceDescriptor->bInterfaceProtocol == 0x01) {
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

            gEndpointOut = epOut;
            gEndpointIn = epIn;
        }

        return driverLength;
    }

    if (interfaceDescriptor->bInterfaceSubClass == 0xFD &&
        interfaceDescriptor->bInterfaceProtocol == 0x13) {

        cursor = tu_desc_next(cursor);

        if (tu_desc_type(cursor) != kSecurityDescriptorType) {
            return 0;
        }

        const std::uint8_t customLength = tu_desc_len(cursor);
        if (customLength == 0 ||
            driverLength + customLength > maxLength) {
            return 0;
        }

        return static_cast<std::uint16_t>(
            driverLength + customLength
        );
    }

    return 0;
}

bool driverControlXfer(
    std::uint8_t rhport,
    std::uint8_t stage,
    tusb_control_request_t const* request
) {
    (void)rhport;
    (void)stage;
    (void)request;

    // No console authentication is implemented in the PC compatibility
    // profile. Standard enumeration is handled by TinyUSB.
    return true;
}

bool driverXfer(
    std::uint8_t rhport,
    std::uint8_t epAddr,
    xfer_result_t result,
    std::uint32_t xferredBytes
) {
    if (epAddr != gEndpointOut) {
        return true;
    }

    if (result == XFER_RESULT_SUCCESS) {
        oag::RumbleCommand decoded = gLatestRumble;

        if (gFeedbackDecoder.decodeRumble(
                gOutBuffer,
                static_cast<std::size_t>(xferredBytes),
                decoded
            )) {
            gLatestRumble = decoded;
            gRumblePending = true;
        }
    }

    if (gEndpointOut != 0) {
        usbd_edpt_xfer(
            rhport,
            gEndpointOut,
            gOutBuffer,
            sizeof(gOutBuffer)
        );
    }

    return true;
}

const usbd_class_driver_t kDriver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "OAG_XINPUT_DEVICE",
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

bool armOutputIfIdle() {
    if (!tud_ready() ||
        gEndpointOut == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, gEndpointOut)) {
        return false;
    }

    if (!usbd_edpt_claim(kDeviceRhPort, gEndpointOut)) {
        return false;
    }

    const bool queued = usbd_edpt_xfer(
        kDeviceRhPort,
        gEndpointOut,
        gOutBuffer,
        sizeof(gOutBuffer)
    );

    if (!queued) {
        usbd_edpt_release(kDeviceRhPort, gEndpointOut);
        return false;
    }

    usbd_edpt_release(kDeviceRhPort, gEndpointOut);
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

    // PC XInput compatibility does not require OAG to impersonate or
    // synthesize a console authentication response.
    return false;
}

namespace oag::firmware {

void PcXinputDevice::task() {
    armOutputIfIdle();
}

bool PcXinputDevice::send(
    const LogicalGamepadState& state
) {
    if (!tud_ready() ||
        gEndpointIn == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, gEndpointIn)) {
        return false;
    }

    const XinputReport next = encoder_.encode(state);

    if (!usbd_edpt_claim(kDeviceRhPort, gEndpointIn)) {
        return false;
    }

    report_ = next;

    const bool queued = usbd_edpt_xfer(
        kDeviceRhPort,
        gEndpointIn,
        reinterpret_cast<std::uint8_t*>(&report_),
        sizeof(report_)
    );

    if (!queued) {
        usbd_edpt_release(kDeviceRhPort, gEndpointIn);
        return false;
    }

    usbd_edpt_release(kDeviceRhPort, gEndpointIn);
    return true;
}

bool PcXinputDevice::sendNeutral() {
    LogicalGamepadState neutral {};
    neutral.connected = true;
    return send(neutral);
}

bool PcXinputDevice::takeRumble(
    RumbleCommand& output
) {
    if (!gRumblePending) {
        return false;
    }

    output = gLatestRumble;
    gRumblePending = false;
    return true;
}

} // namespace oag::firmware
