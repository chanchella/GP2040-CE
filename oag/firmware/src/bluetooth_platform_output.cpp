#include "oag/firmware/bluetooth_platform_output.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "btstack.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "oag/firmware/bluetooth_host_v2.h"
#include "oag/input/gamepad_state.h"

namespace {

oag::firmware::BluetoothPlatformOutput* gBluetoothPlatformOutput = nullptr;

btstack_packet_callback_registration_t gPlatformHciRegistration {};
btstack_packet_callback_registration_t gPlatformSmRegistration {};

void platformHciThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothPlatformOutput != nullptr) {
        gBluetoothPlatformOutput->handleHciPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void platformSmThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothPlatformOutput != nullptr) {
        gBluetoothPlatformOutput->handleSmPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void platformHidsThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothPlatformOutput != nullptr) {
        gBluetoothPlatformOutput->handleHidsPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

// Compatibility-first Generic HID Gamepad report.
// Reference direction: BTstack HOG device example + generic gamepad layouts
// used by ESP32-BLE-Gamepad. No vendor-specific/Xbox BLE emulation is used in
// this first external-Bluetooth baseline.
constexpr std::uint8_t kHidDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID 1

    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,
    0x29, 0x10,       //   16 buttons
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x10,
    0x81, 0x02,

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Hat switch
    0x15, 0x00,
    0x25, 0x07,
    0x35, 0x00,
    0x46, 0x3B, 0x01,
    0x65, 0x14,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x42,       //   Null state allowed
    0x65, 0x00,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x03,

    0x05, 0x01,
    0x09, 0x30,       // X
    0x09, 0x31,       // Y
    0x09, 0x32,       // Z
    0x09, 0x35,       // Rz
    0x16, 0x00, 0x80,
    0x26, 0xFF, 0x7F,
    0x75, 0x10,
    0x95, 0x04,
    0x81, 0x02,

    0x05, 0x02,       // Simulation Controls
    0x09, 0xC5,       // Brake
    0x09, 0xC4,       // Accelerator
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x02,

    0xC0
};

// Exact advertising pattern used by BTstack's official HOG device examples:
// General Discoverable + LE-only persona + HID UUID + Gamepad appearance.
constexpr std::uint8_t kAdvertisingData[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x12, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'O','A','G',' ','U','n','i','v','e','r','s','a','l',' ','P','a','d',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF
    ),
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8
    ),
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

std::int16_t encodeAxis(std::int32_t value) {
    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int16_t>::min();
    }

    if (value <= 0) {
        return static_cast<std::int16_t>(value / 65536);
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) *
        std::numeric_limits<std::int16_t>::max() /
        std::numeric_limits<std::int32_t>::max();

    return static_cast<std::int16_t>(scaled);
}

void storeLe16(
    std::array<std::uint8_t, 13>& report,
    std::size_t offset,
    std::int16_t value
) {
    const auto raw = static_cast<std::uint16_t>(value);
    report[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
    report[offset + 1] = static_cast<std::uint8_t>(raw >> 8);
}

std::uint8_t encodeHat(std::uint8_t dpad) {
    const bool up =
        (dpad & static_cast<std::uint8_t>(oag::DpadBits::Up)) != 0;
    const bool down =
        (dpad & static_cast<std::uint8_t>(oag::DpadBits::Down)) != 0;
    const bool left =
        (dpad & static_cast<std::uint8_t>(oag::DpadBits::Left)) != 0;
    const bool right =
        (dpad & static_cast<std::uint8_t>(oag::DpadBits::Right)) != 0;

    if (up && !down) {
        if (right && !left) return 1;
        if (left && !right) return 7;
        return 0;
    }

    if (down && !up) {
        if (right && !left) return 3;
        if (left && !right) return 5;
        return 4;
    }

    if (right && !left) return 2;
    if (left && !right) return 6;
    return 8;
}

std::array<std::uint8_t, 13> encodeReport(
    const oag::LogicalGamepadState& state
) {
    std::array<std::uint8_t, 13> report {};
    report[2] = 8;

    if (!state.connected) {
        return report;
    }

    std::uint16_t buttons = 0;
    const auto addButton =
        [&](std::uint64_t sourceMask, std::uint8_t bit) {
            if ((state.buttons & sourceMask) != 0) {
                buttons |= static_cast<std::uint16_t>(1u << bit);
            }
        };

    addButton(oag::ButtonSouth, 0);
    addButton(oag::ButtonEast, 1);
    addButton(oag::ButtonWest, 2);
    addButton(oag::ButtonNorth, 3);
    addButton(oag::ButtonLeftBumper, 4);
    addButton(oag::ButtonRightBumper, 5);
    addButton(oag::ButtonLeftStick, 6);
    addButton(oag::ButtonRightStick, 7);
    addButton(oag::ButtonBack, 8);
    addButton(oag::ButtonStart, 9);
    addButton(oag::ButtonGuide, 10);
    addButton(oag::ButtonShare, 11);

    report[0] = static_cast<std::uint8_t>(buttons & 0xFFu);
    report[1] = static_cast<std::uint8_t>(buttons >> 8);
    report[2] = encodeHat(state.dpad);

    storeLe16(report, 3, encodeAxis(state.lx));
    storeLe16(report, 5, encodeAxis(state.ly));
    storeLe16(report, 7, encodeAxis(state.rx));
    storeLe16(report, 9, encodeAxis(state.ry));

    report[11] = static_cast<std::uint8_t>(state.leftTrigger >> 24);
    report[12] = static_cast<std::uint8_t>(state.rightTrigger >> 24);

    return report;
}

} // namespace

namespace oag::firmware {

bool BluetoothPlatformOutput::initialize(BluetoothHostV2& host) {
    if (initialized_) {
        return true;
    }

    host_ = &host;
    gBluetoothPlatformOutput = this;

    // Important: do NOT call l2cap_init(), sm_init(), cyw43_arch_init(), or
    // hci_power_control() here. TRUE GOLDEN BluetoothHostV2 owns the stack.
    // Keep its proven global SM policy (Bonding + NoInputNoOutput) unchanged.

    battery_service_server_init(100);

    device_information_service_server_init();
    device_information_service_server_set_manufacturer_name("OAG");
    device_information_service_server_set_model_number("Universal Pad");
    device_information_service_server_set_firmware_revision(
        "U10F-PM1-G1"
    );
    device_information_service_server_set_pnp_id(
        2,
        0x0000,
        0x0001,
        0x0100
    );

    hids_device_init(
        0,
        kHidDescriptor,
        sizeof(kHidDescriptor)
    );
    hids_device_register_packet_handler(
        platformHidsThunk
    );

    gPlatformHciRegistration.callback = &platformHciThunk;
    hci_add_event_handler(
        &gPlatformHciRegistration
    );

    gPlatformSmRegistration.callback = &platformSmThunk;
    sm_add_event_handler(
        &gPlatformSmRegistration
    );

    initialized_ = true;

    // BluetoothHostV2 powers HCI before this peripheral layer is registered.
    // Usually HCI_STATE_WORKING arrives asynchronously afterwards, but do not
    // leave advertising dependent on that timing. If the controller is already
    // working, enter the exact same advertising path immediately.
    hciWorking_ = hci_get_state() == HCI_STATE_WORKING;
    if (hciWorking_) {
        startAdvertising();
    }

    return true;
}

void BluetoothPlatformOutput::poll() {
    if (
        reportDirty_ &&
        inputSubscribed_ &&
        connectionHandle_ != kInvalidHandle
    ) {
        requestCanSend();
    }
}

void BluetoothPlatformOutput::submit(
    const oag::LogicalGamepadState& state
) {
    const auto next = encodeReport(state);

    if (next == report_) {
        return;
    }

    report_ = next;
    reportDirty_ = true;
    requestCanSend();
}

void BluetoothPlatformOutput::startAdvertising() {
    if (!initialized_ || !hciWorking_ || connected()) {
        return;
    }

    bd_addr_t nullAddress {};
    gap_advertisements_set_params(
        0x0030,
        0x0030,
        0,
        0,
        nullAddress,
        0x07,
        0x00
    );

    gap_advertisements_set_data(
        sizeof(kAdvertisingData),
        const_cast<std::uint8_t*>(kAdvertisingData)
    );

    (void)gap_advertisements_enable(1);
}

void BluetoothPlatformOutput::requestCanSend() {
    if (
        !initialized_ ||
        !inputSubscribed_ ||
        canSendPending_ ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    if (
        hids_device_request_can_send_now_event(
            connectionHandle_
        ) == ERROR_CODE_SUCCESS
    ) {
        canSendPending_ = true;
    }
}

void BluetoothPlatformOutput::sendCurrentReport() {
    if (
        connectionHandle_ == kInvalidHandle ||
        !inputSubscribed_
    ) {
        return;
    }

    const std::uint8_t status =
        hids_device_send_input_report_for_id(
            connectionHandle_,
            kInputReportId,
            report_.data(),
            static_cast<std::uint16_t>(report_.size())
        );

    if (status == ERROR_CODE_SUCCESS) {
        reportDirty_ = false;
    }
}

void BluetoothPlatformOutput::handleHciPacket(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    (void)channel;
    (void)size;

    if (
        packetType != HCI_EVENT_PACKET ||
        packet == nullptr
    ) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (
                btstack_event_state_get_state(packet) ==
                HCI_STATE_WORKING
            ) {
                hciWorking_ = true;
                startAdvertising();
            }
            break;

        case HCI_EVENT_META_GAP:
            if (
                hci_event_gap_meta_get_subevent_code(packet) !=
                GAP_SUBEVENT_LE_CONNECTION_COMPLETE
            ) {
                break;
            } else {
                const std::uint8_t status =
                    gap_subevent_le_connection_complete_get_status(
                        packet
                    );

                if (status != ERROR_CODE_SUCCESS) {
                    break;
                }

                const std::uint8_t role =
                    gap_subevent_le_connection_complete_get_role(
                        packet
                    );

                if (role != HCI_ROLE_SLAVE) {
                    break;
                }

                connectionHandle_ =
                    gap_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );

                peerAddressType_ =
                    gap_subevent_le_connection_complete_get_peer_address_type(
                        packet
                    );

                bd_addr_t address {};
                gap_subevent_le_connection_complete_get_peer_address(
                    packet,
                    address
                );

                std::copy(
                    address,
                    address + peerAddress_.size(),
                    peerAddress_.begin()
                );

                inputSubscribed_ = false;
                canSendPending_ = false;
                reportDirty_ = true;

                if (host_ != nullptr) {
                    host_->setPlatformOutputLinkActive(true);
                }
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            const std::uint16_t handle =
                hci_event_disconnection_complete_get_connection_handle(
                    packet
                );

            if (handle != connectionHandle_) {
                break;
            }

            connectionHandle_ = kInvalidHandle;
            inputSubscribed_ = false;
            canSendPending_ = false;
            reportDirty_ = true;

            if (host_ != nullptr) {
                host_->setPlatformOutputLinkActive(false);
            }

            startAdvertising();
            break;
        }

        default:
            break;
    }
}

void BluetoothPlatformOutput::handleSmPacket(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    (void)channel;
    (void)size;

    if (
        packetType != HCI_EVENT_PACKET ||
        packet == nullptr ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_PAIRING_COMPLETE: {
            const std::uint16_t handle =
                sm_event_pairing_complete_get_handle(packet);

            if (handle != connectionHandle_) {
                break;
            }

            if (
                sm_event_pairing_complete_get_status(packet) !=
                ERROR_CODE_SUCCESS
            ) {
                gap_disconnect(handle);
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            const std::uint16_t handle =
                sm_event_reencryption_complete_get_handle(packet);

            if (handle != connectionHandle_) {
                break;
            }

            const std::uint8_t status =
                sm_event_reencryption_complete_get_status(packet);

            if (status == ERROR_CODE_SUCCESS) {
                break;
            }

            if (status == ERROR_CODE_PIN_OR_KEY_MISSING) {
                gap_delete_bonding(
                    static_cast<bd_addr_type_t>(peerAddressType_),
                    peerAddress_.data()
                );

                // Recovery only: the remote forgot its LTK while the Pico
                // retained it. Fresh first-time pairing is still initiated
                // naturally by the central when it accesses encrypted HIDS.
                sm_request_pairing(handle);
                break;
            }

            gap_disconnect(handle);
            break;
        }

        default:
            break;
    }
}

void BluetoothPlatformOutput::handleHidsPacket(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    (void)channel;
    (void)size;

    if (
        packetType != HCI_EVENT_PACKET ||
        packet == nullptr ||
        hci_event_packet_get_type(packet) != HCI_EVENT_HIDS_META
    ) {
        return;
    }

    switch (hci_event_hids_meta_get_subevent_code(packet)) {
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE: {
            const std::uint16_t handle =
                hids_subevent_input_report_enable_get_con_handle(
                    packet
                );

            const std::uint8_t reportId =
                hids_subevent_input_report_enable_get_report_id(
                    packet
                );

            if (
                handle != connectionHandle_ ||
                reportId != kInputReportId
            ) {
                break;
            }

            inputSubscribed_ =
                hids_subevent_input_report_enable_get_enable(packet) != 0;

            reportDirty_ = true;
            requestCanSend();
            break;
        }

        case HIDS_SUBEVENT_CAN_SEND_NOW: {
            const std::uint16_t handle =
                hids_subevent_can_send_now_get_con_handle(packet);

            if (handle != connectionHandle_) {
                break;
            }

            canSendPending_ = false;
            sendCurrentReport();

            if (reportDirty_) {
                requestCanSend();
            }
            break;
        }

        default:
            break;
    }
}

} // namespace oag::firmware
