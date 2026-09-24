#include "oag/firmware/pc_xinput_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pico/time.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

namespace {

constexpr std::uint8_t kDeviceRhPort = 0;
constexpr std::uint8_t kReceiverDescriptorType = 0x22;
constexpr std::uint8_t kGamepadProtocol = 0x81;
constexpr std::uint8_t kAuxProtocol = 0x82;
constexpr std::size_t kInterfaceCount = 8;
constexpr std::size_t kOutputSlots =
    oag::firmware::PcXinputDevice::kOutputSlots;
constexpr std::size_t kEndpointBufferSize = 32;
constexpr std::size_t kWirelessInputPacketSize = 29;
constexpr std::uint64_t kPresenceHeartbeatUs = 1000000ull;

struct ReceiverInterfaceRuntime {
    std::uint8_t interfaceNumber = 0xFF;
    std::uint8_t protocol = 0;
    std::uint8_t endpointIn = 0;
    std::uint8_t endpointOut = 0;
    std::uint8_t outBuffer[kEndpointBufferSize] {};
};

struct ReceiverSlotRuntime {
    std::uint8_t interfaceNumber = 0xFF;
    std::uint8_t endpointIn = 0;
    std::uint8_t endpointOut = 0;

    oag::XinputReport latestReport {};
    oag::RumbleCommand latestRumble {};

    std::uint8_t txBuffer[kEndpointBufferSize] {};

    bool desiredPresent = false;
    bool announcedPresent = false;
    bool inputPending = false;
    bool presenceReplyPending = false;
    bool announcePending = false;
    bool batteryReplyPending = false;
    bool rumblePending = false;

    std::uint64_t nextPresenceHeartbeatUs = 0;
};

std::array<ReceiverInterfaceRuntime, kInterfaceCount> gInterfaces {};
std::array<ReceiverSlotRuntime, kOutputSlots> gSlots {};

void resetRuntime() {
    gInterfaces = {};
    gSlots = {};

    for (auto& interfaceRuntime : gInterfaces) {
        interfaceRuntime.interfaceNumber = 0xFF;
    }

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

int slotForGamepadInterface(std::uint8_t interfaceNumber) {
    if ((interfaceNumber & 1u) != 0u || interfaceNumber >= kInterfaceCount) {
        return -1;
    }

    const std::size_t slot =
        static_cast<std::size_t>(interfaceNumber / 2u);

    return slot < kOutputSlots
        ? static_cast<int>(slot)
        : -1;
}

int interfaceForOutEndpoint(std::uint8_t endpointAddress) {
    for (std::size_t i = 0; i < gInterfaces.size(); ++i) {
        if (
            gInterfaces[i].endpointOut != 0 &&
            gInterfaces[i].endpointOut == endpointAddress
        ) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool queueIn(
    std::size_t slotIndex,
    const std::uint8_t* data,
    std::size_t length
) {
    if (
        slotIndex >= gSlots.size() ||
        data == nullptr ||
        length == 0 ||
        length > kEndpointBufferSize
    ) {
        return false;
    }

    auto& runtime = gSlots[slotIndex];

    if (
        !tud_ready() ||
        runtime.endpointIn == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, runtime.endpointIn)
    ) {
        return false;
    }

    if (!usbd_edpt_claim(kDeviceRhPort, runtime.endpointIn)) {
        return false;
    }

    std::memset(runtime.txBuffer, 0, sizeof(runtime.txBuffer));
    std::memcpy(runtime.txBuffer, data, length);

    const bool queued = usbd_edpt_xfer(
        kDeviceRhPort,
        runtime.endpointIn,
        runtime.txBuffer,
        static_cast<std::uint16_t>(length)
    );

    if (!queued) {
        usbd_edpt_release(kDeviceRhPort, runtime.endpointIn);
        return false;
    }

    usbd_edpt_release(kDeviceRhPort, runtime.endpointIn);
    return true;
}

bool sendPresence(std::size_t slotIndex, bool present) {
    const std::uint8_t packet[2] = {
        0x08,
        static_cast<std::uint8_t>(present ? 0x80 : 0x00),
    };

    return queueIn(slotIndex, packet, sizeof(packet));
}

bool sendAnnounce(std::size_t slotIndex) {
    // Captured from a genuine receiver/controller session. The identity bytes
    // are informational to xusb22; controller ownership still comes from the
    // receiver interface/endpoint pair.
    const std::uint8_t packet[kWirelessInputPacketSize] = {
        0x00, 0x0F, 0x00, 0xF0,
        0xF0, 0xCC, 0xE0, 0xCB, 0x7A, 0xD0,
        0x58, 0x91, 0xB3, 0xF0, 0x00, 0x09,
        0x13, 0xA3, 0x20, 0x1D, 0x30, 0x03,
        0x40, 0x01, 0x50, 0x01, 0xFF, 0xFF, 0xFF,
    };

    return queueIn(slotIndex, packet, sizeof(packet));
}

bool sendBatteryState(std::size_t slotIndex) {
    // Full battery, NiMH wire type 0. Windows maps this to the XInput NiMH
    // battery type instead of UNKNOWN.
    const std::uint8_t packet[5] = {
        0x00, 0x00, 0x00, 0x10, 0xC0,
    };

    return queueIn(slotIndex, packet, sizeof(packet));
}

bool sendInput(std::size_t slotIndex) {
    if (slotIndex >= gSlots.size()) {
        return false;
    }

    std::uint8_t packet[kWirelessInputPacketSize] {};
    packet[0] = 0x00;
    packet[1] = 0x01;
    packet[2] = 0x00;
    packet[3] = 0xF0;

    oag::XinputReport report = gSlots[slotIndex].latestReport;

    // Genuine wireless packets use 0x13 here, rather than the wired 0x14.
    // xusb22 then interprets the standard controller payload beginning at
    // packet byte 4.
    report.reportId = 0x00;
    report.reportSize = 0x13;

    std::memcpy(
        packet + 4,
        &report,
        sizeof(report)
    );

    return queueIn(slotIndex, packet, sizeof(packet));
}

bool armOutputIfIdle(std::size_t interfaceIndex) {
    if (interfaceIndex >= gInterfaces.size()) {
        return false;
    }

    auto& runtime = gInterfaces[interfaceIndex];

    if (
        !tud_ready() ||
        runtime.endpointOut == 0 ||
        !usbd_edpt_ready(kDeviceRhPort, runtime.endpointOut)
    ) {
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

void parseGamepadOut(
    std::size_t slotIndex,
    const std::uint8_t* data,
    std::size_t length
) {
    if (
        slotIndex >= gSlots.size() ||
        data == nullptr
    ) {
        return;
    }

    auto& slot = gSlots[slotIndex];

    // Receiver rumble:
    // 00 01 0F C0 00 <strong> <weak>
    if (
        length >= 7 &&
        data[0] == 0x00 &&
        data[1] == 0x01 &&
        data[2] == 0x0F &&
        data[3] == 0xC0
    ) {
        oag::RumbleCommand next {};
        next.leftMotor = data[5];
        next.rightMotor = data[6];
        next.generation = slot.latestRumble.generation + 1u;

        slot.latestRumble = next;
        slot.rumblePending = true;
        return;
    }

    // Windows polls controller presence about every 2.5 seconds.
    if (
        length >= 4 &&
        data[0] == 0x08 &&
        data[1] == 0x00 &&
        data[2] == 0x0F &&
        data[3] == 0xC0
    ) {
        slot.presenceReplyPending = true;

        if (slot.desiredPresent) {
            slot.announcePending = true;
        }

        return;
    }

    // Battery/state request.
    if (
        length >= 4 &&
        data[0] == 0x00 &&
        data[1] == 0x00 &&
        data[2] == 0x00 &&
        data[3] == 0x40
    ) {
        if (slot.desiredPresent) {
            slot.batteryReplyPending = true;
        }
        return;
    }

    // Capabilities request. Re-announce the attached virtual controller.
    if (
        length >= 4 &&
        data[0] == 0x00 &&
        data[1] == 0x00 &&
        data[2] == 0x02 &&
        data[3] == 0x80
    ) {
        if (slot.desiredPresent) {
            slot.announcePending = true;
        }
        return;
    }

    // LED/player-slot commands are intentionally accepted and ignored.
}

void serviceSlot(std::size_t slotIndex, std::uint64_t nowUs) {
    if (slotIndex >= gSlots.size()) {
        return;
    }

    auto& slot = gSlots[slotIndex];

    if (slot.endpointIn == 0) {
        return;
    }

    if (slot.desiredPresent != slot.announcedPresent) {
        if (!sendPresence(slotIndex, slot.desiredPresent)) {
            return;
        }

        slot.announcedPresent = slot.desiredPresent;
        slot.presenceReplyPending = false;

        if (slot.desiredPresent) {
            slot.announcePending = true;
            slot.inputPending = true;
            slot.nextPresenceHeartbeatUs =
                nowUs + kPresenceHeartbeatUs;
        } else {
            slot.announcePending = false;
            slot.batteryReplyPending = false;
            slot.inputPending = false;
            slot.nextPresenceHeartbeatUs = 0;
        }

        return;
    }

    if (slot.presenceReplyPending) {
        if (sendPresence(slotIndex, slot.desiredPresent)) {
            slot.presenceReplyPending = false;
            if (slot.desiredPresent) {
                slot.nextPresenceHeartbeatUs =
                    nowUs + kPresenceHeartbeatUs;
            }
        }
        return;
    }

    if (!slot.desiredPresent) {
        return;
    }

    if (slot.batteryReplyPending) {
        if (sendBatteryState(slotIndex)) {
            slot.batteryReplyPending = false;
        }
        return;
    }

    if (slot.announcePending) {
        if (sendAnnounce(slotIndex)) {
            slot.announcePending = false;
        }
        return;
    }

    // Steam/SDL may open the receiver after the initial connection event.
    // Repeating presence prevents that late opener from missing the child pad.
    if (
        slot.nextPresenceHeartbeatUs == 0 ||
        nowUs >= slot.nextPresenceHeartbeatUs
    ) {
        if (sendPresence(slotIndex, true)) {
            slot.nextPresenceHeartbeatUs =
                nowUs + kPresenceHeartbeatUs;
        }
        return;
    }

    if (slot.inputPending) {
        if (sendInput(slotIndex)) {
            slot.inputPending = false;
        }
    }
}

std::uint16_t driverOpen(
    std::uint8_t rhport,
    tusb_desc_interface_t const* interfaceDescriptor,
    std::uint16_t maxLength
) {
    if (
        interfaceDescriptor == nullptr ||
        interfaceDescriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        interfaceDescriptor->bInterfaceSubClass != 0x5D ||
        (
            interfaceDescriptor->bInterfaceProtocol != kGamepadProtocol &&
            interfaceDescriptor->bInterfaceProtocol != kAuxProtocol
        ) ||
        interfaceDescriptor->bNumEndpoints != 2 ||
        interfaceDescriptor->bInterfaceNumber >= kInterfaceCount
    ) {
        return 0;
    }

    const std::uint8_t interfaceNumber =
        interfaceDescriptor->bInterfaceNumber;
    const std::uint8_t protocol =
        interfaceDescriptor->bInterfaceProtocol;

    const std::uint8_t* cursor =
        reinterpret_cast<const std::uint8_t*>(interfaceDescriptor);

    std::uint16_t driverLength =
        static_cast<std::uint16_t>(sizeof(tusb_desc_interface_t));

    cursor = tu_desc_next(cursor);

    if (tu_desc_type(cursor) != kReceiverDescriptorType) {
        return 0;
    }

    const std::uint8_t customLength = tu_desc_len(cursor);
    const std::uint8_t expectedCustomLength =
        protocol == kGamepadProtocol ? 0x14 : 0x0C;

    if (
        customLength != expectedCustomLength ||
        driverLength + customLength +
            interfaceDescriptor->bNumEndpoints *
                sizeof(tusb_desc_endpoint_t) >
            maxLength
    ) {
        return 0;
    }

    driverLength = static_cast<std::uint16_t>(
        driverLength + customLength +
        interfaceDescriptor->bNumEndpoints *
            sizeof(tusb_desc_endpoint_t)
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

    auto& interfaceRuntime = gInterfaces[interfaceNumber];
    interfaceRuntime = {};
    interfaceRuntime.interfaceNumber = interfaceNumber;
    interfaceRuntime.protocol = protocol;
    interfaceRuntime.endpointIn = epIn;
    interfaceRuntime.endpointOut = epOut;

    if (protocol == kGamepadProtocol) {
        const int slotIndex = slotForGamepadInterface(interfaceNumber);
        if (slotIndex < 0) {
            return 0;
        }

        auto& slot = gSlots[static_cast<std::size_t>(slotIndex)];
        slot = {};
        slot.interfaceNumber = interfaceNumber;
        slot.endpointIn = epIn;
        slot.endpointOut = epOut;
    }

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
    return true;
}

bool driverXfer(
    std::uint8_t rhport,
    std::uint8_t epAddr,
    xfer_result_t result,
    std::uint32_t xferredBytes
) {
    (void)rhport;

    const int interfaceIndex =
        interfaceForOutEndpoint(epAddr);

    if (interfaceIndex < 0) {
        // IN completion.
        return true;
    }

    auto& interfaceRuntime =
        gInterfaces[static_cast<std::size_t>(interfaceIndex)];

    if (
        result == XFER_RESULT_SUCCESS &&
        interfaceRuntime.protocol == kGamepadProtocol
    ) {
        const int slotIndex =
            slotForGamepadInterface(interfaceRuntime.interfaceNumber);

        if (slotIndex >= 0) {
            parseGamepadOut(
                static_cast<std::size_t>(slotIndex),
                interfaceRuntime.outBuffer,
                static_cast<std::size_t>(xferredBytes)
            );
        }
    }

    // Re-arm immediately; task() is also a safety net.
    armOutputIfIdle(static_cast<std::size_t>(interfaceIndex));
    return true;
}

const usbd_class_driver_t kDriver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "OAG_XBOX360_WIRELESS_RECEIVER",
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
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    // Genuine receivers answer this vendor request with a 7-byte hardware
    // identity. Use the Pico unique ID so each OAG unit remains stable.
    if (
        request != nullptr &&
        request->bmRequestType == 0xC0 &&
        request->bRequest == 0x01 &&
        request->wValue == 0x0001 &&
        request->wIndex == 0x0000 &&
        request->wLength == 7
    ) {
        static std::uint8_t serialReply[7] {};
        pico_unique_board_id_t id {};
        pico_get_unique_board_id(&id);

        for (std::size_t i = 0; i < sizeof(serialReply); ++i) {
            serialReply[i] = id.id[i + 1];
        }

        return tud_control_xfer(
            rhport,
            request,
            serialReply,
            sizeof(serialReply)
        );
    }

    return false;
}

namespace oag::firmware {

void PcXinputDevice::task() {
    for (std::size_t i = 0; i < gInterfaces.size(); ++i) {
        armOutputIfIdle(i);
    }

    const std::uint64_t nowUs = time_us_64();

    for (std::size_t slot = 0; slot < kOutputSlots; ++slot) {
        serviceSlot(slot, nowUs);
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

    runtime.desiredPresent = state.connected;

    if (!state.connected) {
        runtime.inputPending = false;
        return true;
    }

    runtime.latestReport =
        encoders_[logicalSlot].encode(state);
    runtime.latestReport.reportId = 0x00;
    runtime.latestReport.reportSize = 0x13;
    runtime.inputPending = true;

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
