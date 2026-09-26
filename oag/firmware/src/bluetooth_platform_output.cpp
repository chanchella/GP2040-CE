#include "oag/firmware/bluetooth_platform_output.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "btstack.h"
#include "pico/async_context.h"
#include "pico/cyw43_arch.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "oag_ble_platform.h"
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

// Arduino-Pico JoystickBLE compatibility profile.
//
// This report map mirrors Earle Philhower's TUD_HID_REPORT_DESC_GAMEPAD16
// layout: six signed 16-bit axes first, then one 8-bit hat, then 32 buttons.
// Report ID remains 1 because this BLE persona exposes only a joystick.
constexpr std::uint8_t kHidDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID 1

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x30,       //   X
    0x09, 0x31,       //   Y
    0x09, 0x32,       //   Z
    0x09, 0x35,       //   Rz
    0x09, 0x33,       //   Rx
    0x09, 0x34,       //   Ry
    0x16, 0x01, 0x80, //   Logical Min -32767
    0x26, 0xFF, 0x7F, //   Logical Max  32767
    0x95, 0x06,       //   Report Count 6
    0x75, 0x10,       //   Report Size 16
    0x81, 0x02,       //   Input Data,Var,Abs

    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Hat switch
    0x15, 0x01,       //   Logical Min 1
    0x25, 0x08,       //   Logical Max 8
    0x35, 0x00,       //   Physical Min 0
    0x46, 0x3B, 0x01, //   Physical Max 315
    0x95, 0x01,       //   Report Count 1
    0x75, 0x08,       //   Report Size 8
    0x81, 0x02,       //   Input Data,Var,Abs

    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Min 1
    0x29, 0x20,       //   Usage Max 32
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x20,       //   32 buttons
    0x75, 0x01,
    0x81, 0x02,

    0xC0
};

class BtstackContextLock {
public:
    BtstackContextLock()
        : context_(cyw43_arch_async_context()) {
        if (context_ != nullptr) {
            async_context_acquire_lock_blocking(context_);
        }
    }

    ~BtstackContextLock() {
        if (context_ != nullptr) {
            async_context_release_lock(context_);
        }
    }

    BtstackContextLock(const BtstackContextLock&) = delete;
    BtstackContextLock& operator=(const BtstackContextLock&) = delete;

private:
    async_context_t* context_;
};

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

hids_device_report_t gHidReportStorage[2] {};

std::int16_t encodeSignedAxis(std::int32_t value) {
    if (value <= std::numeric_limits<std::int32_t>::min()) {
        return static_cast<std::int16_t>(-32767);
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) * 32767ll /
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());

    return static_cast<std::int16_t>(
        std::clamp<std::int64_t>(scaled, -32767ll, 32767ll)
    );
}

std::int16_t encodeTriggerAxis(std::uint32_t value) {
    const std::int64_t scaled =
        (static_cast<std::uint64_t>(value) * 65534ull) /
        0xFFFFFFFFull;

    return static_cast<std::int16_t>(
        static_cast<std::int64_t>(scaled) - 32767ll
    );
}

void storeLe16(
    std::array<std::uint8_t, 17>& report,
    std::size_t offset,
    std::int16_t value
) {
    const auto raw = static_cast<std::uint16_t>(value);
    report[offset] = static_cast<std::uint8_t>(raw & 0xFFu);
    report[offset + 1] = static_cast<std::uint8_t>(raw >> 8);
}

void storeLe32(
    std::array<std::uint8_t, 17>& report,
    std::size_t offset,
    std::uint32_t value
) {
    report[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    report[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    report[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    report[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
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
        if (right && !left) return 2;
        if (left && !right) return 8;
        return 1;
    }

    if (down && !up) {
        if (right && !left) return 4;
        if (left && !right) return 6;
        return 5;
    }

    if (right && !left) return 3;
    if (left && !right) return 7;
    return 0;
}

std::array<std::uint8_t, 17> encodeReport(
    const oag::LogicalGamepadState& state
) {
    std::array<std::uint8_t, 17> report {};

    // Arduino-Pico GAMEPAD16 order:
    // x,y,z,rz,rx,ry (12 bytes), hat (1 byte), buttons (4 bytes).
    storeLe16(report, 0, 0);
    storeLe16(report, 2, 0);
    storeLe16(report, 4, 0);
    storeLe16(report, 6, 0);
    storeLe16(report, 8, -32767);
    storeLe16(report, 10, -32767);
    report[12] = 0;
    storeLe32(report, 13, 0);

    if (!state.connected) {
        return report;
    }

    storeLe16(report, 0, encodeSignedAxis(state.lx));
    storeLe16(report, 2, encodeSignedAxis(state.ly));
    storeLe16(report, 4, encodeSignedAxis(state.rx));
    storeLe16(report, 6, encodeSignedAxis(state.ry));
    storeLe16(report, 8, encodeTriggerAxis(state.leftTrigger));
    storeLe16(report, 10, encodeTriggerAxis(state.rightTrigger));
    report[12] = encodeHat(state.dpad);

    std::uint32_t buttons = 0;
    const auto addButton =
        [&](std::uint64_t sourceMask, std::uint8_t bit) {
            if ((state.buttons & sourceMask) != 0) {
                buttons |= static_cast<std::uint32_t>(1u << bit);
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

    storeLe32(report, 13, buttons);
    return report;
}

void platformGetReportThunk(
    hci_con_handle_t,
    hid_report_type_t reportType,
    std::uint16_t reportId,
    std::uint16_t maxReportSize,
    std::uint8_t* outReport
) {
    if (
        gBluetoothPlatformOutput == nullptr ||
        outReport == nullptr ||
        reportType != HID_REPORT_TYPE_INPUT ||
        reportId != 1u
    ) {
        return;
    }

    gBluetoothPlatformOutput->copyCurrentInputReport(
        outReport,
        maxReportSize
    );
}

} // namespace

namespace oag::firmware {

bool BluetoothPlatformOutput::initializeStandalone() {
    if (initialized_) {
        return true;
    }

    // OUT13 diagnostic: BluetoothPlatformOutput owns the complete Bluetooth
    // lifecycle. BluetoothHostV2 is not initialized at all. This mirrors the
    // hardware-proven Arduino-Pico JoystickBLE startup order inside UI5K while
    // leaving USB/PIO/XInput/KM untouched.
    if (cyw43_arch_init() != PICO_OK) {
        return false;
    }

    host_ = nullptr;
    gBluetoothPlatformOutput = this;

    {
        BtstackContextLock btLock;

        l2cap_init();

        sm_init();
        sm_set_io_capabilities(
            IO_CAPABILITY_NO_INPUT_NO_OUTPUT
        );
        sm_set_authentication_requirements(
            SM_AUTHREQ_SECURE_CONNECTION |
            SM_AUTHREQ_BONDING
        );

        att_server_init(
            profile_data,
            nullptr,
            nullptr
        );

        battery_service_server_init(100);

        device_information_service_server_init();
        device_information_service_server_set_manufacturer_name("OAG");
        device_information_service_server_set_model_number("Universal Pad");
        device_information_service_server_set_firmware_revision(
            "U10F-PM1-UI5K-BT-OUT13-STANDALONE-OWNER"
        );
        device_information_service_server_set_pnp_id(
            0x02,
            0xCAFE,
            0x4016,
            0x0100
        );

        gap_set_local_name("OAG Universal Pad");

        hids_device_init_with_storage(
            0,
            kHidDescriptor,
            sizeof(kHidDescriptor),
            2,
            gHidReportStorage
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

        // Match PicoBluetoothBLEHID: configure/enable advertising before
        // powering HCI. The controller applies it once HCI reaches WORKING.
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

        initialized_ = true;
        hciWorking_ = false;

        hci_power_control(HCI_POWER_ON);
    }

    return true;
}

void BluetoothPlatformOutput::poll() {
    serviceConnectionSelfTest();

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

    if (next == liveReport_) {
        return;
    }

    liveReport_ = next;

    // During the connection proof, keep recording the real controller state
    // but do not let it overwrite the synthetic report. The instant the proof
    // completes, serviceConnectionSelfTest() copies liveReport_ to report_.
    if (selfTestActive_) {
        return;
    }

    if (next == report_) {
        return;
    }

    report_ = next;
    reportDirty_ = true;
    requestCanSend();
}

void BluetoothPlatformOutput::startConnectionSelfTest() {
    if (
        !inputSubscribed_ ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    selfTestActive_ = true;
    selfTestStartedMs_ = btstack_run_loop_get_time_ms();
    selfTestStep_ = 0;

    // Start from an explicit neutral GAMEPAD16 report.
    report_.fill(0);
    storeLe16(report_, 0, 0);
    storeLe16(report_, 2, 0);
    storeLe16(report_, 4, 0);
    storeLe16(report_, 6, 0);
    storeLe16(report_, 8, -32767);
    storeLe16(report_, 10, -32767);
    report_[12] = 0;
    storeLe32(report_, 13, 0);

    reportDirty_ = true;
    requestCanSend();
}

void BluetoothPlatformOutput::serviceConnectionSelfTest() {
    if (
        !selfTestActive_ ||
        !inputSubscribed_ ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    constexpr std::uint32_t kStepMs = 900u;
    constexpr std::uint8_t kStepCount = 6u;

    const std::uint32_t nowMs = btstack_run_loop_get_time_ms();
    const std::uint32_t elapsedMs = nowMs - selfTestStartedMs_;
    const std::uint8_t nextStep =
        static_cast<std::uint8_t>(elapsedMs / kStepMs);

    if (nextStep >= kStepCount) {
        selfTestActive_ = false;
        selfTestStep_ = kStepCount;

        // Critical OUT9 handoff: from this point onward, Bluetooth output is
        // driven only by the real Primary controller state.
        report_ = liveReport_;
        reportDirty_ = true;
        requestCanSend();
        return;
    }

    if (nextStep == selfTestStep_ && elapsedMs >= kStepMs) {
        return;
    }

    if (nextStep == selfTestStep_ && elapsedMs < kStepMs) {
        // Step zero must be emitted immediately after subscription.
    } else {
        selfTestStep_ = nextStep;
    }

    std::array<std::uint8_t, 17> probe {};
    storeLe16(probe, 0, 0);
    storeLe16(probe, 2, 0);
    storeLe16(probe, 4, 0);
    storeLe16(probe, 6, 0);
    storeLe16(probe, 8, -32767);
    storeLe16(probe, 10, -32767);
    probe[12] = 0;
    storeLe32(probe, 13, 0);

    switch (nextStep) {
        case 0:
            // Button 1 / South pressed.
            storeLe32(probe, 13, 1u);
            break;

        case 1:
            // Neutral.
            break;

        case 2:
            // D-pad Down.
            probe[12] = 5u;
            break;

        case 3:
            // Neutral.
            break;

        case 4:
            // Left stick X full right.
            storeLe16(probe, 0, 32767);
            break;

        case 5:
        default:
            // Neutral before live handoff.
            break;
    }

    if (probe != report_) {
        report_ = probe;
        reportDirty_ = true;
        requestCanSend();
    }
}

void BluetoothPlatformOutput::startAdvertising() {
    if (!initialized_ || !hciWorking_ || connected()) {
        return;
    }

    BtstackContextLock btLock;
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

    BtstackContextLock btLock;

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

    BtstackContextLock btLock;

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

void BluetoothPlatformOutput::copyCurrentInputReport(
    std::uint8_t* out,
    std::uint16_t maxSize
) const {
    if (out == nullptr || maxSize == 0) {
        return;
    }

    const std::size_t count =
        std::min<std::size_t>(report_.size(), maxSize);

    std::memcpy(out, report_.data(), count);

    if (count < maxSize) {
        std::memset(out + count, 0, maxSize - count);
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
                // OUT13 advertising was configured/enabled before HCI power-on,
                // matching the hardware-proven Arduino-Pico lifecycle.
                hciWorking_ = true;
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
                selfTestActive_ = false;
                selfTestStartedMs_ = 0;
                selfTestStep_ = 0;

                // G2: Android-compatible Just Works Secure Connections.
                // This policy is activated only while a central owns our
                // peripheral/output link. Background controller discovery is
                // already paused for this interval, so new input-peer pairing
                // continues to use the UI5K Bonding-only policy.
                sm_set_authentication_requirements(
                    SM_AUTHREQ_BONDING |
                    SM_AUTHREQ_SECURE_CONNECTION
                );

                if (host_ != nullptr) {
                    host_->setPlatformOutputLinkActive(true);
                }
            }
            break;

        case HCI_EVENT_LE_META:
            if (
                hci_event_le_meta_get_subevent_code(packet) !=
                HCI_SUBEVENT_LE_CONNECTION_COMPLETE
            ) {
                break;
            } else {
                const std::uint8_t status =
                    hci_subevent_le_connection_complete_get_status(packet);

                if (status != ERROR_CODE_SUCCESS) {
                    break;
                }

                const std::uint8_t role =
                    hci_subevent_le_connection_complete_get_role(packet);

                if (role != HCI_ROLE_SLAVE) {
                    break;
                }

                const std::uint16_t handle =
                    hci_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );

                // GAP_META is BTstack's preferred normalized event, but some
                // shared Host+Peripheral paths can still surface the raw LE
                // meta event first. Accept either source and make connection
                // adoption idempotent.
                connectionHandle_ = handle;
                peerAddressType_ =
                    hci_subevent_le_connection_complete_get_peer_address_type(
                        packet
                    );

                bd_addr_t address {};
                hci_subevent_le_connection_complete_get_peer_address(
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
                selfTestActive_ = false;
                selfTestStartedMs_ = 0;
                selfTestStep_ = 0;

                sm_set_authentication_requirements(
                    SM_AUTHREQ_BONDING |
                    SM_AUTHREQ_SECURE_CONNECTION
                );

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
            selfTestActive_ = false;
            selfTestStartedMs_ = 0;
            selfTestStep_ = 0;
            report_ = liveReport_;

            // OUT8 keeps the shared stack in the same Secure Connections +
            // Bonding policy that the hardware-proven standalone JoystickBLE
            // uses. Do not downgrade after the phone disconnects, otherwise a
            // later HOGP CCCD subscription can fall back to the old UI5K policy.
            sm_set_authentication_requirements(
                SM_AUTHREQ_SECURE_CONNECTION |
                SM_AUTHREQ_BONDING
            );

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

            if (reportId != kInputReportId) {
                break;
            }

            // The HIDS subscription event is authoritative for the ATT link.
            // If the earlier GAP/LE connection event was missed by this
            // coexisting host+peripheral backend, adopt the handle here instead
            // of discarding the first usable HID subscription.
            if (connectionHandle_ == kInvalidHandle) {
                connectionHandle_ = handle;
                canSendPending_ = false;
                reportDirty_ = true;

                sm_set_authentication_requirements(
                    SM_AUTHREQ_BONDING |
                    SM_AUTHREQ_SECURE_CONNECTION
                );

                if (host_ != nullptr) {
                    host_->setPlatformOutputLinkActive(true);
                }
            }

            if (handle != connectionHandle_) {
                break;
            }

            inputSubscribed_ =
                hids_subevent_input_report_enable_get_enable(packet) != 0;

            if (inputSubscribed_) {
                // OUT10: Android HOGP is now genuinely ready. Start the
                // visible self-test first, then unlock BluetoothHostV2 so the
                // physical controller can connect while the phone remains
                // subscribed to our BLE gamepad output.
                startConnectionSelfTest();

                if (host_ != nullptr) {
                    host_->unlockInputDiscoveryAfterPlatformSubscription();
                }
            } else {
                selfTestActive_ = false;
                report_ = liveReport_;
                reportDirty_ = true;
            }

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
