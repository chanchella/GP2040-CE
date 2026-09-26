/*
 * OAG UI5K Arduino-Pico BLE3 output backend.
 *
 * HID/GATT/runtime behavior is intentionally aligned with:
 *   earlephilhower/arduino-pico
 *   revision 7a00f15279c74064a39549c1fdce81e877027983
 *   libraries/JoystickBLE + libraries/HID_Bluetooth
 * Upstream license: LGPL-2.1-or-later.
 *
 * JoypadOS contributes only the coexistence model: one ATT/HIDS Device owner
 * plus Bluetooth Host clients on the same BTstack/CYW43 controller.
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

oag::firmware::JoypadBleOutput* gArduinoBleOutput = nullptr;
btstack_packet_callback_registration_t gArduinoBleHciRegistration {};
btstack_packet_callback_registration_t gArduinoBleSmRegistration {};

// Exact logical shape of Arduino-Pico's:
//   TUD_HID_REPORT_DESC_GAMEPAD16(HID_REPORT_ID(1))
//
// Report payload (excluding Report ID) is 17 bytes:
//   X,Y,Z,Rz,Rx,Ry = signed 16-bit
//   Hat             = 8-bit
//   Buttons         = 32-bit
constexpr std::uint8_t kArduinoGamepad16Descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       // Report ID (1)

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x30,       // X
    0x09, 0x31,       // Y
    0x09, 0x32,       // Z
    0x09, 0x35,       // Rz
    0x09, 0x33,       // Rx
    0x09, 0x34,       // Ry
    0x16, 0x01, 0x80, // Logical Min -32767
    0x26, 0xFF, 0x7F, // Logical Max  32767
    0x95, 0x06,       // Report Count 6
    0x75, 0x10,       // Report Size 16
    0x81, 0x02,       // Input (Data,Variable,Absolute)

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x39,       // Hat switch
    0x15, 0x01,       // Logical Min 1
    0x25, 0x08,       // Logical Max 8
    0x35, 0x00,       // Physical Min 0
    0x46, 0x3B, 0x01, // Physical Max 315
    0x95, 0x01,       // Report Count 1
    0x75, 0x08,       // Report Size 8
    0x81, 0x02,       // Input (Data,Variable,Absolute)

    0x05, 0x09,       // Usage Page (Button)
    0x19, 0x01,       // Usage Min 1
    0x29, 0x20,       // Usage Max 32
    0x15, 0x00,       // Logical Min 0
    0x25, 0x01,       // Logical Max 1
    0x95, 0x20,       // Report Count 32
    0x75, 0x01,       // Report Size 1
    0x81, 0x02,       // Input (Data,Variable,Absolute)

    0xC0              // End Collection
};

// Arduino-Pico joystick-only runtime allocates two report slots:
// one joystick Input report plus one Feature report.
hids_device_report_t gArduinoBleReportStorage[2] {};

constexpr char kBleName[] = "OAG BLE3 Gamepad";

// Arduino-Pico _buildAdvData() layout:
// flags + complete local name + HID service UUID + appearance.
constexpr std::uint8_t kAdvertisingData[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'O','A','G',' ','B','L','E','3',' ','G','a','m','e','p','a','d',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF
    ),
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8
    ),
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

static_assert(sizeof(kAdvertisingData) == 29);

void arduinoBlePacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gArduinoBleOutput != nullptr) {
        gArduinoBleOutput->handlePacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

std::int16_t signed32ToSigned16(std::int32_t value) {
    const std::int64_t shifted =
        static_cast<std::int64_t>(value) -
        static_cast<std::int64_t>(
            std::numeric_limits<std::int32_t>::min()
        );

    const std::int64_t scaled =
        (shifted * 65534ll) / 0xFFFFFFFFll - 32767ll;

    return static_cast<std::int16_t>(
        std::clamp<std::int64_t>(
            scaled,
            -32767ll,
            32767ll
        )
    );
}

std::int16_t triggerToSigned16(std::uint32_t value) {
    const std::int64_t scaled =
        (static_cast<std::uint64_t>(value) * 65534ull) /
        0xFFFFFFFFull;

    return static_cast<std::int16_t>(
        static_cast<std::int64_t>(scaled) - 32767ll
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

    // Arduino-Pico Joystick uses 0 as the neutral hat value even though its
    // descriptor declares the directional range as 1..8.
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

    report.x = signed32ToSigned16(state.lx);
    report.y = signed32ToSigned16(state.ly);
    report.z = signed32ToSigned16(state.rx);
    report.rz = signed32ToSigned16(state.ry);
    report.rx = triggerToSigned16(state.leftTrigger);
    report.ry = triggerToSigned16(state.rightTrigger);
    report.hat = encodeHat(state.dpad);

    const auto mapButton =
        [&](std::uint64_t mask, std::uint8_t bit) {
            if ((state.buttons & mask) != 0) {
                report.buttons |=
                    static_cast<std::uint32_t>(1u << bit);
            }
        };

    mapButton(oag::ButtonSouth, 0);
    mapButton(oag::ButtonEast, 1);
    mapButton(oag::ButtonWest, 2);
    mapButton(oag::ButtonNorth, 3);
    mapButton(oag::ButtonLeftBumper, 4);
    mapButton(oag::ButtonRightBumper, 5);
    mapButton(oag::ButtonBack, 6);
    mapButton(oag::ButtonStart, 7);
    mapButton(oag::ButtonLeftStick, 8);
    mapButton(oag::ButtonRightStick, 9);
    mapButton(oag::ButtonGuide, 10);
    mapButton(oag::ButtonShare, 11);

    return report;
}

bool JoypadBleOutput::initialize(BluetoothHostV2& host) {
    if (initialized_) {
        return true;
    }

    host_ = &host;
    gArduinoBleOutput = this;

    // JoypadOS coexistence rule: one ATT server owner.
    // The ATT layout itself now mirrors Arduino-Pico PicoBluetoothBLEHID.
    att_server_init(
        profile_data,
        nullptr,
        nullptr
    );

    // Arduino-Pico startHID() initializes these services but does not override
    // Device Information fields in joystick-only mode.
    battery_service_server_init(100);
    device_information_service_server_init();

    // Arduino-Pico joystick-only: one Input report + one Feature slot.
    hids_device_init_with_storage(
        0,
        kArduinoGamepad16Descriptor,
        sizeof(kArduinoGamepad16Descriptor),
        2,
        gArduinoBleReportStorage
    );

    sm_set_io_capabilities(
        IO_CAPABILITY_NO_INPUT_NO_OUTPUT
    );
    sm_set_authentication_requirements(
        SM_AUTHREQ_SECURE_CONNECTION |
        SM_AUTHREQ_BONDING
    );

    // Fresh identity defeats Android's per-device GATT/HID cache from BLE1/2.
    bd_addr_t ble3Address = {
        0xC2, 0xA5, 0xB3, 0x55, 0x10, 0x03
    };
    gap_random_address_set(ble3Address);
    gap_random_address_set_mode(
        GAP_RANDOM_ADDRESS_TYPE_STATIC
    );

    gap_set_local_name(kBleName);

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

    gArduinoBleHciRegistration.callback =
        &arduinoBlePacketThunk;
    hci_add_event_handler(
        &gArduinoBleHciRegistration
    );

    gArduinoBleSmRegistration.callback =
        &arduinoBlePacketThunk;
    sm_add_event_handler(
        &gArduinoBleSmRegistration
    );

    hids_device_register_packet_handler(
        arduinoBlePacketThunk
    );

    initialized_ = true;
    setAdvertising(true);
    return true;
}

void JoypadBleOutput::setAdvertising(bool enabled) {
    if (!initialized_ || advertising_ == enabled) {
        return;
    }

    advertising_ = enabled;
    gap_advertisements_enable(enabled ? 1 : 0);
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

    // Arduino-Pico sends joystick reports only in Report Protocol.
    if (protocolMode_ != 1u) {
        pendingDirty_ = false;
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

void JoypadBleOutput::openHidsSession(
    std::uint16_t handle
) {
    connectionHandle_ = handle;
    gamepadSubscribed_ = true;
    advertising_ = false;
    canSendPending_ = false;

    startSelfTest();

    if (host_ != nullptr) {
        host_->unlockInputDiscoveryAfterPlatformSubscription();
    }

    requestCanSend();
}

void JoypadBleOutput::startSelfTest() {
    selfTestActive_ = true;
    selfTestStartedMs_ = btstack_run_loop_get_time_ms();
    selfTestStep_ = 0xFFu;
    pendingDirty_ = true;
}

void JoypadBleOutput::serviceSelfTest() {
    if (
        !selfTestActive_ ||
        !gamepadSubscribed_
    ) {
        return;
    }

    // Match the hardware-proven standalone probe cadence:
    // B1 down/up -> D-pad Down/neutral -> LX right/center.
    constexpr std::uint32_t kStepMs = 1200u;
    constexpr std::uint8_t kStepCount = 6u;

    const std::uint32_t elapsed =
        btstack_run_loop_get_time_ms() -
        selfTestStartedMs_;

    const std::uint8_t step =
        static_cast<std::uint8_t>(
            elapsed / kStepMs
        );

    if (step >= kStepCount) {
        selfTestActive_ = false;
        pendingReport_ = liveReport_;
        pendingDirty_ = true;
        requestCanSend();
        return;
    }

    if (step == selfTestStep_) {
        return;
    }

    selfTestStep_ = step;

    GamepadReport probe {};

    switch (step) {
        case 0:
            probe.buttons = 0x00000001u;
            break;

        case 2:
            probe.hat = 5u;
            break;

        case 4:
            probe.x = 32767;
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
            protocolMode_ = 1u;

            pendingReport_ = liveReport_;
            pendingDirty_ = true;

            if (host_ != nullptr) {
                host_->setPlatformOutputLinkActive(false);
            }

            advertising_ = false;
            setAdvertising(true);
            break;
        }

        case SM_EVENT_JUST_WORKS_REQUEST: {
            const std::uint16_t handle =
                sm_event_just_works_request_get_handle(packet);

            if (handle == rawLinkHandle_) {
                sm_just_works_confirm(handle);
            }
            break;
        }

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST: {
            const std::uint16_t handle =
                sm_event_numeric_comparison_request_get_handle(
                    packet
                );

            if (handle == rawLinkHandle_) {
                sm_numeric_comparison_confirm(handle);
            }
            break;
        }

        case HCI_EVENT_HIDS_META:
            switch (
                hci_event_hids_meta_get_subevent_code(packet)
            ) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    // Exact Arduino-Pico readiness rule: no Report-ID filter.
                    openHidsSession(
                        hids_subevent_input_report_enable_get_con_handle(
                            packet
                        )
                    );
                    break;

                case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
                    // PicoBluetoothBLEHID treats this as an opened HID session
                    // too, even in joystick-only mode.
                    openHidsSession(
                        hids_subevent_boot_keyboard_input_report_enable_get_con_handle(
                            packet
                        )
                    );
                    break;

                case HIDS_SUBEVENT_PROTOCOL_MODE:
                    protocolMode_ =
                        hids_subevent_protocol_mode_get_protocol_mode(
                            packet
                        );
                    break;

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
