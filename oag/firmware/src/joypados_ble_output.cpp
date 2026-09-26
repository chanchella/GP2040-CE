/*
 * OAG JoypadOS-derived BLE output backend.
 *
 * Architecture and standard BLE HID profile are derived from:
 *   joypad-ai/joypad-os, src/bt/ble_output/ble_output.c
 *   pinned design reference: 969c232c4e3332e6df6af524b001e1d65613f6bc
 * Upstream license: Apache-2.0.
 *
 * This is an OAG adaptation, not a verbatim copy. OAG's own state/routing,
 * device identity, lifecycle hooks, and self-test are implemented here.
 */

#include "oag/firmware/joypados_ble_output.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "btstack.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "oag_joypados_ble.h"
#include "oag/firmware/bluetooth_host_v2.h"
#include "oag/input/gamepad_state.h"

namespace {

oag::firmware::JoypadBleOutput* gJoypadBleOutput = nullptr;
btstack_packet_callback_registration_t gJoypadBleHciRegistration {};
btstack_packet_callback_registration_t gJoypadBleSmRegistration {};

void joypadBlePacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gJoypadBleOutput != nullptr) {
        gJoypadBleOutput->handlePacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

// JoypadOS Standard composite HID descriptor.
// Keyboard = ID 1, Mouse = ID 2, Gamepad = ID 3,
// Player Indicator output = ID 4, Battery feature = ID 5.
constexpr std::uint8_t kStandardHidDescriptor[] = {
    // Keyboard, Report ID 1
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05,
    0x91,0x02, 0x95,0x01, 0x75,0x03, 0x91,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07,
    0x19,0x00, 0x29,0x65, 0x81,0x00,
    0xC0,

    // Mouse, Report ID 2
    0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x02,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x05, 0x15,0x00, 0x25,0x01,
    0x95,0x05, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x03, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31, 0x15,0x81, 0x25,0x7F,
    0x75,0x08, 0x95,0x02, 0x81,0x06,
    0x09,0x38, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01,
    0x81,0x06,
    0xC0, 0xC0,

    // Gamepad, Report ID 3
    0x05,0x01, 0x09,0x05, 0xA1,0x01, 0x85,0x03,
    // 16 buttons
    0x05,0x09, 0x19,0x01, 0x29,0x10, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x10, 0x81,0x02,
    // Hat: 1..8, 0 null
    0x05,0x01, 0x09,0x39, 0x15,0x01, 0x25,0x08, 0x35,0x00,
    0x46,0x3B,0x01, 0x65,0x14, 0x75,0x08, 0x95,0x01, 0x81,0x42,
    0x65,0x00,
    // Six 16-bit axes, logical/physical 0..32767
    0x05,0x01, 0x15,0x00, 0x27,0xFF,0x7F,0x00,0x00,
    0x35,0x00, 0x47,0xFF,0x7F,0x00,0x00,
    0x09,0x30, 0x09,0x31, 0x09,0x32, 0x09,0x35, 0x09,0x33, 0x09,0x34,
    0x75,0x10, 0x95,0x06, 0x81,0x02,
    0xC0,

    // Player Indicator Output, Report ID 4
    0x05,0x01, 0x09,0x05, 0xA1,0x01, 0x85,0x04,
    0x05,0x08, 0x09,0x4B, 0x15,0x00, 0x25,0xFF,
    0x75,0x08, 0x95,0x01, 0x91,0x02,
    0xC0,

    // Battery Feature, Report ID 5
    0x05,0x01, 0x09,0x05, 0xA1,0x01, 0x85,0x05,
    0x05,0x06, 0x09,0x20, 0x15,0x00, 0x26,0xFF,0x00,
    0x75,0x08, 0x95,0x01, 0xB1,0x02,
    0xC0
};

hids_device_report_t gJoypadBleReportStorage[12] {};

// Keep the primary legacy advertising packet tiny, as JoypadOS does.
// Complete name lives in scan response to stay safely below 31 bytes.
constexpr std::uint8_t kAdvertisingData[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF
    ),
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8
    ),
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

constexpr std::uint8_t kScanResponse[] = {
    0x12, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'O','A','G',' ','U','n','i','v','e','r','s','a','l',' ','P','a','d'
};

std::int16_t signedAxisToUnsigned15(std::int32_t value) {
    const std::int64_t shifted =
        static_cast<std::int64_t>(value) -
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());

    const std::uint64_t scaled =
        static_cast<std::uint64_t>(shifted) * 32767ull /
        0xFFFFFFFFull;

    return static_cast<std::int16_t>(
        std::min<std::uint64_t>(scaled, 32767ull)
    );
}

std::int16_t triggerToUnsigned15(std::uint32_t value) {
    return static_cast<std::int16_t>(
        static_cast<std::uint64_t>(value) * 32767ull /
        0xFFFFFFFFull
    );
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

    if (up && right && !down && !left) return 2;
    if (up && left && !down && !right) return 8;
    if (down && right && !up && !left) return 4;
    if (down && left && !up && !right) return 6;
    if (up && !down) return 1;
    if (right && !left) return 3;
    if (down && !up) return 5;
    if (left && !right) return 7;
    return 0;
}

} // namespace

namespace oag::firmware {

JoypadBleOutput::GamepadReport JoypadBleOutput::encode(
    const oag::LogicalGamepadState& state
) {
    GamepadReport report {};

    if (!state.connected) {
        return report;
    }

    std::uint16_t buttons = 0;
    const auto mapButton = [&](std::uint64_t mask, std::uint8_t bit) {
        if ((state.buttons & mask) != 0) {
            buttons |= static_cast<std::uint16_t>(1u << bit);
        }
    };

    mapButton(oag::ButtonSouth, 0);
    mapButton(oag::ButtonEast, 1);
    mapButton(oag::ButtonWest, 2);
    mapButton(oag::ButtonNorth, 3);
    mapButton(oag::ButtonLeftBumper, 4);
    mapButton(oag::ButtonRightBumper, 5);
    mapButton(oag::ButtonBack, 8);
    mapButton(oag::ButtonStart, 9);
    mapButton(oag::ButtonLeftStick, 10);
    mapButton(oag::ButtonRightStick, 11);
    mapButton(oag::ButtonGuide, 12);
    mapButton(oag::ButtonShare, 13);

    report.buttonsLo = static_cast<std::uint8_t>(buttons & 0xFFu);
    report.buttonsHi = static_cast<std::uint8_t>(buttons >> 8);
    report.hat = encodeHat(state.dpad);
    report.lx = signedAxisToUnsigned15(state.lx);
    report.ly = signedAxisToUnsigned15(state.ly);
    report.rx = signedAxisToUnsigned15(state.rx);
    report.ry = signedAxisToUnsigned15(state.ry);
    report.lt = triggerToUnsigned15(state.leftTrigger);
    report.rt = triggerToUnsigned15(state.rightTrigger);
    return report;
}

bool JoypadBleOutput::initialize(BluetoothHostV2& host) {
    if (initialized_) {
        return true;
    }

    host_ = &host;
    gJoypadBleOutput = this;

    // JoypadOS coexistence rule: BLE output owns the one ATT server.
    att_server_init(
        profile_data,
        nullptr,
        nullptr
    );

    battery_service_server_init(100);

    device_information_service_server_init();
    device_information_service_server_set_manufacturer_name("OAG");
    device_information_service_server_set_model_number("Universal Pad");
    device_information_service_server_set_software_revision(
        "UI5K-JOYPADOS-BLE1"
    );
    device_information_service_server_set_pnp_id(
        0x02,
        0xCAFE,
        0x4016,
        0x0100
    );

    hids_device_init_with_storage(
        0,
        kStandardHidDescriptor,
        sizeof(kStandardHidDescriptor),
        12,
        gJoypadBleReportStorage
    );

    sm_set_io_capabilities(
        IO_CAPABILITY_NO_INPUT_NO_OUTPUT
    );
    sm_set_authentication_requirements(
        SM_AUTHREQ_SECURE_CONNECTION |
        SM_AUTHREQ_BONDING
    );

    gap_set_local_name("OAG Universal Pad");

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
    gap_scan_response_set_data(
        sizeof(kScanResponse),
        const_cast<std::uint8_t*>(kScanResponse)
    );

    gJoypadBleHciRegistration.callback = &joypadBlePacketThunk;
    hci_add_event_handler(&gJoypadBleHciRegistration);

    gJoypadBleSmRegistration.callback = &joypadBlePacketThunk;
    sm_add_event_handler(&gJoypadBleSmRegistration);

    hids_device_register_packet_handler(
        joypadBlePacketThunk
    );

    initialized_ = true;
    setAdvertising(true);
    return true;
}

void JoypadBleOutput::setAdvertising(bool enabled) {
    if (!initialized_ || advertising_ == enabled) {
        return;
    }

    const std::uint8_t status =
        gap_advertisements_enable(enabled ? 1 : 0);

    if (status == ERROR_CODE_SUCCESS) {
        advertising_ = enabled;
    }
}

void JoypadBleOutput::submit(
    const oag::LogicalGamepadState& state
) {
    liveReport_ = encode(state);

    if (selfTestActive_) {
        return;
    }

    if (
        std::memcmp(
            &liveReport_,
            &pendingReport_,
            sizeof(GamepadReport)
        ) == 0
    ) {
        return;
    }

    pendingReport_ = liveReport_;
    pendingDirty_ = true;
    requestCanSend();
}

void JoypadBleOutput::poll() {
    serviceSelfTest();

    if (pendingDirty_) {
        requestCanSend();
    }
}

void JoypadBleOutput::requestCanSend() {
    if (
        !gamepadSubscribed_ ||
        connectionHandle_ == kInvalidHandle ||
        canSendPending_
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

void JoypadBleOutput::sendPending() {
    if (
        !gamepadSubscribed_ ||
        connectionHandle_ == kInvalidHandle ||
        !pendingDirty_
    ) {
        return;
    }

    const std::uint8_t status =
        hids_device_send_input_report_for_id(
            connectionHandle_,
            kGamepadReportId,
            reinterpret_cast<const std::uint8_t*>(
                &pendingReport_
            ),
            sizeof(GamepadReport)
        );

    if (status == ERROR_CODE_SUCCESS) {
        lastSentReport_ = pendingReport_;
        pendingDirty_ = false;
    }
}

void JoypadBleOutput::startSelfTest() {
    selfTestActive_ = true;
    selfTestStartedMs_ = btstack_run_loop_get_time_ms();
    selfTestStep_ = 0;
    pendingDirty_ = true;
}

void JoypadBleOutput::serviceSelfTest() {
    if (
        !selfTestActive_ ||
        !gamepadSubscribed_
    ) {
        return;
    }

    constexpr std::uint32_t kStepMs = 900u;
    constexpr std::uint8_t kStepCount = 6u;

    const std::uint32_t nowMs =
        btstack_run_loop_get_time_ms();

    const std::uint32_t elapsed =
        nowMs - selfTestStartedMs_;

    const std::uint8_t step =
        static_cast<std::uint8_t>(elapsed / kStepMs);

    if (step >= kStepCount) {
        selfTestActive_ = false;
        pendingReport_ = liveReport_;
        pendingDirty_ = true;
        requestCanSend();
        return;
    }

    if (step == selfTestStep_ && elapsed >= kStepMs) {
        return;
    }

    selfTestStep_ = step;

    GamepadReport probe {};

    switch (step) {
        case 0:
            probe.buttonsLo = 0x01u;
            break;
        case 2:
            probe.hat = 5u;
            break;
        case 4:
            probe.lx = 32767;
            break;
        default:
            break;
    }

    pendingReport_ = probe;
    pendingDirty_ = true;
    requestCanSend();
}

void JoypadBleOutput::handlePacket(
    std::uint8_t packetType,
    std::uint16_t,
    std::uint8_t* packet,
    std::uint16_t
) {
    if (
        packetType != HCI_EVENT_PACKET ||
        packet == nullptr
    ) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_LE_META:
            if (
                hci_event_le_meta_get_subevent_code(packet) ==
                HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&
                hci_subevent_le_connection_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS &&
                hci_subevent_le_connection_complete_get_role(packet) ==
                HCI_ROLE_SLAVE
            ) {
                rawLinkHandle_ =
                    hci_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );

                setAdvertising(false);

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

            if (
                handle != rawLinkHandle_ &&
                handle != connectionHandle_
            ) {
                break;
            }

            rawLinkHandle_ = kInvalidHandle;
            connectionHandle_ = kInvalidHandle;
            gamepadSubscribed_ = false;
            canSendPending_ = false;
            selfTestActive_ = false;
            pendingReport_ = liveReport_;
            pendingDirty_ = true;

            if (host_ != nullptr) {
                host_->setPlatformOutputLinkActive(false);
            }

            advertising_ = false;
            setAdvertising(true);
            break;
        }

        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(
                sm_event_just_works_request_get_handle(packet)
            );
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(
                sm_event_passkey_display_number_get_handle(packet)
            );
            break;

        case HCI_EVENT_HIDS_META:
            switch (
                hci_event_hids_meta_get_subevent_code(packet)
            ) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE: {
                    const std::uint8_t reportId =
                        hids_subevent_input_report_enable_get_report_id(
                            packet
                        );

                    if (reportId != kGamepadReportId) {
                        break;
                    }

                    const bool enabled =
                        hids_subevent_input_report_enable_get_enable(
                            packet
                        ) != 0;

                    if (!enabled) {
                        gamepadSubscribed_ = false;
                        break;
                    }

                    connectionHandle_ =
                        hids_subevent_input_report_enable_get_con_handle(
                            packet
                        );

                    gamepadSubscribed_ = true;
                    advertising_ = false;
                    startSelfTest();

                    if (host_ != nullptr) {
                        host_->unlockInputDiscoveryAfterPlatformSubscription();
                    }

                    requestCanSend();
                    break;
                }

                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    canSendPending_ = false;
                    sendPending();
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

} // namespace oag::firmware
