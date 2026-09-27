#include <Arduino.h>

#include <JoystickBLE.h>
#include <PicoBluetoothBLEHID.h>
#include <BluetoothLock.h>

#include "btstack.h"
#include "ble/gatt-service/hids_host.h"

// ============================================================================
// OAG Arduino-Pico 6.1.1 BLE Dual-Role Probe
//
// PURPOSE
// -------
// Prove on one Raspberry Pi Pico 2 W / one CYW43 radio:
//
//   Bluetooth Controller (Peripheral)
//           -> Pico as BLE Central / HIDS Host
//           -> Pico logical bridge
//           -> Pico as BLE Peripheral / HIDS Device
//           -> Windows PC
//
// OUTPUT SIDE
// -----------
// Uses the exact Arduino-Pico JoystickBLE -> PicoBluetoothBLEHID path that
// already passed hardware testing on this Pico and on Windows.
//
// INPUT SIDE
// ----------
// Uses the same BTstack HIDS Host lifecycle as the historical working
// BluetoothHIDMaster generation:
//   scan UUID 0x1812 -> stop scan -> gap_connect -> SM pairing ->
//   hids_host_connect -> parse live reports.
//
// This is a standalone diagnostic. It does NOT modify or execute UI5K runtime.
// ============================================================================

namespace {

constexpr char kOutputName[] = "OAG DUALROLE BT PROBE";

constexpr uint32_t kSelfTestStepMs = 1200;
constexpr uint8_t kSelfTestSteps = 6;

btstack_packet_callback_registration_t gHciRegistration {};
btstack_packet_callback_registration_t gSmRegistration {};

uint8_t gHidsDescriptorStorage[8192] {};

volatile bool gHciWorking = false;
volatile bool gScanActive = false;
volatile bool gCandidateReady = false;
volatile bool gConnectPending = false;
volatile bool gInputReady = false;

bd_addr_t gCandidateAddress {};
bd_addr_type_t gCandidateAddressType = BD_ADDR_TYPE_LE_PUBLIC;

hci_con_handle_t gInputHandle = HCI_CON_HANDLE_INVALID;
uint16_t gHidsCid = 0;
bool gHidsConnectStarted = false;

bool gOutputWasReady = false;
bool gSelfTestActive = false;
bool gSelfTestDone = false;
uint32_t gSelfTestStartedMs = 0;
uint8_t gSelfTestStep = 0xFF;

uint32_t gLastScanRestartMs = 0;

// --------------------------------------------------------------------------
// Forward declarations
// --------------------------------------------------------------------------

void startInputScan();
void startInputHids();
void handleGattClientEvent(
    uint8_t packetType,
    uint16_t channel,
    uint8_t *packet,
    uint16_t size
);

// --------------------------------------------------------------------------
// JoystickBLE helpers
// --------------------------------------------------------------------------

void outputNeutral() {
    JoystickBLE.button(1, false);
    JoystickBLE.hat(-1);
    JoystickBLE.X(512);
    JoystickBLE.Y(512);
    JoystickBLE.Z(512);
    JoystickBLE.Zrotate(512);
    JoystickBLE.sliderLeft(512);
    JoystickBLE.sliderRight(512);
    for (uint8_t i = 2; i <= 32; ++i) {
        JoystickBLE.button(i, false);
    }
    JoystickBLE.send_now();
}

void applySelfTestStep(uint8_t step) {
    outputNeutral();

    switch (step) {
        case 0:
            JoystickBLE.button(1, true);
            break;
        case 1:
            JoystickBLE.button(1, false);
            break;
        case 2:
            JoystickBLE.hat(180);
            break;
        case 3:
            JoystickBLE.hat(-1);
            break;
        case 4:
            JoystickBLE.X(1023);
            break;
        case 5:
        default:
            JoystickBLE.X(512);
            break;
    }

    JoystickBLE.send_now();
}

void serviceOutputSelfTest() {
    const bool outputReady = PicoBluetoothBLEHID.connected();

    if (outputReady && !gOutputWasReady) {
        gOutputWasReady = true;
        gSelfTestActive = true;
        gSelfTestDone = false;
        gSelfTestStartedMs = millis();
        gSelfTestStep = 0xFF;
    }

    if (!outputReady) {
        gOutputWasReady = false;
        gSelfTestActive = false;
        gSelfTestDone = false;
        gSelfTestStep = 0xFF;
        return;
    }

    if (!gSelfTestActive) {
        return;
    }

    const uint32_t elapsed = millis() - gSelfTestStartedMs;
    const uint8_t step = static_cast<uint8_t>(elapsed / kSelfTestStepMs);

    if (step >= kSelfTestSteps) {
        outputNeutral();
        gSelfTestActive = false;
        gSelfTestDone = true;
        return;
    }

    if (step != gSelfTestStep) {
        gSelfTestStep = step;
        applySelfTestStep(step);
    }
}

// --------------------------------------------------------------------------
// Generic HID report -> JoystickBLE bridge
//
// This first diagnostic deliberately keeps mapping simple:
// X/Y/Rx/Ry are treated as signed 16-bit-style stick axes.
// Z/Rz are accepted as trigger-like 0..1023 values when possible.
// Buttons and hat are forwarded directly.
// --------------------------------------------------------------------------

int axisSigned16To10Bit(int32_t value) {
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;

    const int64_t shifted = static_cast<int64_t>(value) + 32768;
    return static_cast<int>((shifted * 1023) / 65535);
}

int axisUnsignedTo10Bit(int32_t value) {
    if (value < 0) value = 0;

    if (value <= 1023) {
        return static_cast<int>(value);
    }

    if (value > 65535) value = 65535;
    return static_cast<int>((static_cast<int64_t>(value) * 1023) / 65535);
}

void forwardInputReport(
    const uint8_t *descriptor,
    uint16_t descriptorLength,
    const uint8_t *report,
    uint16_t reportLength
) {
    if (
        descriptor == nullptr ||
        descriptorLength == 0 ||
        report == nullptr ||
        reportLength == 0 ||
        !PicoBluetoothBLEHID.connected()
    ) {
        return;
    }

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        descriptorLength,
        HID_REPORT_TYPE_INPUT,
        report,
        reportLength
    );

    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t rz = 0;
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t hat = 0;
    uint32_t buttons = 0;

    bool haveX = false;
    bool haveY = false;
    bool haveZ = false;
    bool haveRz = false;
    bool haveRx = false;
    bool haveRy = false;
    bool haveHat = false;

    while (btstack_hid_parser_has_more(&parser)) {
        uint16_t usagePage = 0;
        uint16_t usage = 0;
        int32_t value = 0;

        btstack_hid_parser_get_field(
            &parser,
            &usagePage,
            &usage,
            &value
        );

        if (usagePage == 0x01) {
            switch (usage) {
                case 0x30:
                    x = value;
                    haveX = true;
                    break;
                case 0x31:
                    y = value;
                    haveY = true;
                    break;
                case 0x32:
                    z = value;
                    haveZ = true;
                    break;
                case 0x35:
                    rz = value;
                    haveRz = true;
                    break;
                case 0x33:
                    rx = value;
                    haveRx = true;
                    break;
                case 0x34:
                    ry = value;
                    haveRy = true;
                    break;
                case 0x39:
                    hat = value;
                    haveHat = true;
                    break;
                default:
                    break;
            }
        } else if (
            usagePage == 0x09 &&
            usage >= 1 &&
            usage <= 32 &&
            value
        ) {
            buttons |= (1u << (usage - 1));
        }
    }

    if (haveX) JoystickBLE.X(axisSigned16To10Bit(x));
    if (haveY) JoystickBLE.Y(axisSigned16To10Bit(y));
    if (haveZ) JoystickBLE.Z(axisUnsignedTo10Bit(z));
    if (haveRz) JoystickBLE.Zrotate(axisUnsignedTo10Bit(rz));
    if (haveRx) JoystickBLE.sliderLeft(axisSigned16To10Bit(rx));
    if (haveRy) JoystickBLE.sliderRight(axisSigned16To10Bit(ry));

    if (haveHat && hat >= 0 && hat <= 8) {
        JoystickBLE.hat(
            static_cast<HID_Joystick::HatPosition>(hat)
        );
    } else {
        JoystickBLE.hat(-1);
    }

    for (uint8_t button = 1; button <= 32; ++button) {
        JoystickBLE.button(
            button,
            (buttons & (1u << (button - 1))) != 0
        );
    }

    JoystickBLE.send_now();
}

// --------------------------------------------------------------------------
// HIDS Host / GATT
// --------------------------------------------------------------------------

void handleGattClientEvent(
    uint8_t packetType,
    uint16_t channel,
    uint8_t *packet,
    uint16_t size
) {
    (void)packetType;
    (void)channel;
    (void)size;

    if (
        packet == nullptr ||
        hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META
    ) {
        return;
    }

    switch (
        hci_event_gattservice_meta_get_subevent_code(packet)
    ) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            const uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(packet);

            if (status == ERROR_CODE_SUCCESS) {
                gInputReady = true;
            } else if (gInputHandle != HCI_CON_HANDLE_INVALID) {
                gap_disconnect(gInputHandle);
            }
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            if (!gInputReady || gHidsCid == 0) {
                break;
            }

            const uint8_t serviceIndex =
                gattservice_subevent_hid_report_get_service_index(packet);

            const uint8_t *descriptor =
                hids_host_descriptor_storage_get_descriptor_data(
                    gHidsCid,
                    serviceIndex
                );

            const uint16_t descriptorLength =
                hids_host_descriptor_storage_get_descriptor_len(
                    gHidsCid,
                    serviceIndex
                );

            forwardInputReport(
                descriptor,
                descriptorLength,
                gattservice_subevent_hid_report_get_report(packet),
                gattservice_subevent_hid_report_get_report_len(packet)
            );
            break;
        }

        default:
            break;
    }
}

void startInputHids() {
    if (
        gInputHandle == HCI_CON_HANDLE_INVALID ||
        gHidsConnectStarted
    ) {
        return;
    }

    uint16_t cid = 0;

    const uint8_t status = hids_host_connect(
        gInputHandle,
        handleGattClientEvent,
        HID_PROTOCOL_MODE_REPORT,
        &cid
    );

    if (status == ERROR_CODE_SUCCESS) {
        gHidsCid = cid;
        gHidsConnectStarted = true;
    }
}

// --------------------------------------------------------------------------
// Security Manager events
//
// JoystickBLE / PicoBluetoothBLEHID already owns and confirms Just Works /
// Numeric Comparison. This handler only observes the controller connection and
// starts HIDS Host after pairing or re-encryption.
// --------------------------------------------------------------------------

void smPacketHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t *packet,
    uint16_t size
) {
    (void)channel;
    (void)size;

    if (
        packetType != HCI_EVENT_PACKET ||
        packet == nullptr ||
        gInputHandle == HCI_CON_HANDLE_INVALID
    ) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_PAIRING_COMPLETE: {
            const hci_con_handle_t handle =
                sm_event_pairing_complete_get_handle(packet);

            if (handle != gInputHandle) {
                break;
            }

            if (
                sm_event_pairing_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS
            ) {
                startInputHids();
            } else {
                gap_disconnect(handle);
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            const hci_con_handle_t handle =
                sm_event_reencryption_complete_get_handle(packet);

            if (handle != gInputHandle) {
                break;
            }

            if (
                sm_event_reencryption_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS
            ) {
                startInputHids();
            } else {
                gap_disconnect(handle);
            }
            break;
        }

        default:
            break;
    }
}

// --------------------------------------------------------------------------
// HCI events
// --------------------------------------------------------------------------

void hciPacketHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t *packet,
    uint16_t size
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
            gHciWorking =
                btstack_event_state_get_state(packet) ==
                HCI_STATE_WORKING;
            break;

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (
                !gScanActive ||
                gCandidateReady ||
                gInputHandle != HCI_CON_HANDLE_INVALID
            ) {
                break;
            }

            const uint8_t *data =
                gap_event_advertising_report_get_data(packet);
            const uint8_t dataLength =
                gap_event_advertising_report_get_data_length(packet);

            if (
                !ad_data_contains_uuid16(
                    dataLength,
                    data,
                    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
                )
            ) {
                break;
            }

            gap_event_advertising_report_get_address(
                packet,
                gCandidateAddress
            );

            gCandidateAddressType =
                static_cast<bd_addr_type_t>(
                    gap_event_advertising_report_get_address_type(packet)
                );

            gCandidateReady = true;
            gScanActive = false;
            break;
        }

        case HCI_EVENT_META_GAP: {
            if (
                hci_event_gap_meta_get_subevent_code(packet) !=
                    GAP_SUBEVENT_LE_CONNECTION_COMPLETE
            ) {
                break;
            }

            const uint8_t status =
                gap_subevent_le_connection_complete_get_status(packet);

            if (status != ERROR_CODE_SUCCESS) {
                if (gConnectPending) {
                    gConnectPending = false;
                    gInputHandle = HCI_CON_HANDLE_INVALID;
                }
                break;
            }

            const uint8_t role =
                gap_subevent_le_connection_complete_get_role(packet);

            // Pico is MASTER/Central only for the physical controller link.
            // The Windows output link is SLAVE/Peripheral and is owned by
            // PicoBluetoothBLEHID.
            if (role != HCI_ROLE_MASTER || !gConnectPending) {
                break;
            }

            gInputHandle =
                gap_subevent_le_connection_complete_get_connection_handle(
                    packet
                );

            gConnectPending = false;
            gHidsCid = 0;
            gHidsConnectStarted = false;
            gInputReady = false;

            // Match the historical BluetoothHIDMaster controller policy.
            sm_set_authentication_requirements(
                SM_AUTHREQ_BONDING
            );
            sm_request_pairing(gInputHandle);
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            const hci_con_handle_t handle =
                hci_event_disconnection_complete_get_connection_handle(packet);

            if (handle == gInputHandle) {
                gInputHandle = HCI_CON_HANDLE_INVALID;
                gHidsCid = 0;
                gHidsConnectStarted = false;
                gInputReady = false;
                gCandidateReady = false;
                gConnectPending = false;
                gScanActive = false;
                gLastScanRestartMs = millis();
            }
            break;
        }

        default:
            break;
    }
}

// --------------------------------------------------------------------------
// Controller discovery
// --------------------------------------------------------------------------

void startInputScan() {
    if (
        !gHciWorking ||
        !PicoBluetoothBLEHID.connected() ||
        gSelfTestActive ||
        gInputHandle != HCI_CON_HANDLE_INVALID ||
        gConnectPending ||
        gCandidateReady ||
        gScanActive
    ) {
        return;
    }

    // Once the Windows HIDS Device link is established, restore the exact
    // historical input-side controller pairing policy.
    sm_set_authentication_requirements(
        SM_AUTHREQ_BONDING
    );

    gap_set_scan_params(
        0,
        75,
        50,
        0
    );

    gap_start_scan();
    gScanActive = true;
    gLastScanRestartMs = millis();
}

void serviceCandidateConnect() {
    if (
        !gCandidateReady ||
        gConnectPending ||
        gInputHandle != HCI_CON_HANDLE_INVALID
    ) {
        return;
    }

    {
        BluetoothLock lock;
        gap_stop_scan();

        const uint8_t status = gap_connect(
            gCandidateAddress,
            gCandidateAddressType
        );

        if (status == ERROR_CODE_SUCCESS) {
            gConnectPending = true;
        } else {
            gCandidateReady = false;
            gScanActive = false;
            gLastScanRestartMs = millis();
        }
    }

    gCandidateReady = false;
}

} // namespace

void setup() {
    // Exact hardware-proven output path.
    JoystickBLE.begin(
        kOutputName,
        kOutputName
    );

    JoystickBLE.setBattery(100);
    JoystickBLE.useManualSend(true);

    // Add only the historical HIDS Host side to the already-owned BTstack.
    gatt_client_init();

    hids_host_init(
        gHidsDescriptorStorage,
        sizeof(gHidsDescriptorStorage)
    );

    gHciRegistration.callback = hciPacketHandler;
    hci_add_event_handler(
        &gHciRegistration
    );

    gSmRegistration.callback = smPacketHandler;
    sm_add_event_handler(
        &gSmRegistration
    );

    // If HCI reached WORKING before our observer was registered, recover state.
    gHciWorking = hci_get_state() == HCI_STATE_WORKING;

    outputNeutral();
}

void loop() {
    serviceOutputSelfTest();

    // Controller discovery starts only after the known-good Windows output
    // HIDS link has subscribed and the automatic output self-test is complete.
    if (
        PicoBluetoothBLEHID.connected() &&
        gSelfTestDone &&
        !gInputReady
    ) {
        if (
            !gScanActive &&
            !gConnectPending &&
            !gCandidateReady &&
            gInputHandle == HCI_CON_HANDLE_INVALID &&
            millis() - gLastScanRestartMs >= 1000
        ) {
            startInputScan();
        }
    }

    serviceCandidateConnect();

    delay(1);
}
