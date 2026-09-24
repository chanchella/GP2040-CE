#include "oag/firmware/bluetooth_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "btstack.h"
#include "ble/gatt-service/hids_client.h"

namespace {

oag::firmware::BluetoothRuntime* gBluetoothRuntime = nullptr;

void hciPacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handleHciPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void classicHidPacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handleClassicHidPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void leHidPacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handleLeHidPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void smPacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handleSmPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

btstack_packet_callback_registration_t gHciRegistration {};
btstack_packet_callback_registration_t gSmRegistration {};

bool looksLikeClassicPeripheral(std::uint32_t classOfDevice) {
    // Major Device Class 0x05 = Peripheral. This includes joystick/gamepad,
    // keyboard, mouse and combo HID devices. Protocol classification happens
    // later from the HID descriptor, not from CoD alone.
    return (classOfDevice & 0x1F00u) == 0x0500u;
}

} // namespace

namespace oag::firmware {

bool BluetoothRuntime::initialize(
    BluetoothRuntimeObserver& observer
) {
    if (initialized_) {
        return true;
    }

    // USB Host is intentionally initialized by FirmwareCore before this call.
    // Poll architecture keeps CYW43/BTstack servicing in the same main loop.
    if (cyw43_arch_init() != PICO_OK) {
        return false;
    }

    observer_ = &observer;
    gBluetoothRuntime = this;

    l2cap_init();

    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    gatt_client_init();

    hid_host_init(
        classicDescriptorStorage_.data(),
        static_cast<std::uint16_t>(
            classicDescriptorStorage_.size()
        )
    );
    hid_host_register_packet_handler(classicHidPacketThunk);

    hids_client_init(
        leDescriptorStorage_.data(),
        static_cast<std::uint16_t>(
            leDescriptorStorage_.size()
        )
    );

    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_SNIFF_MODE |
        LM_LINK_POLICY_ENABLE_ROLE_SWITCH
    );
    hci_set_master_slave_policy(HCI_ROLE_MASTER);

    gHciRegistration.callback = &hciPacketThunk;
    hci_add_event_handler(&gHciRegistration);

    gSmRegistration.callback = &smPacketThunk;
    sm_add_event_handler(&gSmRegistration);

    // Classic HID devices are allowed to initiate a reconnect/pair as well.
    gap_discoverable_control(1);

    initialized_ = true;
    hci_power_control(HCI_POWER_ON);
    return true;
}

void BluetoothRuntime::poll() {
    if (!initialized_) {
        return;
    }

    cyw43_arch_poll();

    if (
        hciWorking_ &&
        discoveryPhase_ == DiscoveryPhase::LeScan &&
        !leConnectPending_ &&
        time_us_64() >= leScanDeadlineUs_
    ) {
        gap_stop_scan();
        startClassicInquiry();
    }
}

bool BluetoothRuntime::beginDiscovery() {
    if (!initialized_ || !hciWorking_) {
        return false;
    }

    resumeDiscovery();
    return true;
}

std::size_t BluetoothRuntime::activeConnectionCount() const {
    std::size_t count = 0;

    for (const ClassicLink& link : classicLinks_) {
        if (link.active) {
            ++count;
        }
    }

    for (const LeLink& link : leLinks_) {
        if (link.active) {
            ++count;
        }
    }

    return count;
}

bool BluetoothRuntime::hasFreeConnectionBudget() const {
    return activeConnectionCount() < kConnectionBudget;
}

BluetoothRuntime::ClassicLink*
BluetoothRuntime::findClassicByCid(std::uint16_t hidCid) {
    for (ClassicLink& link : classicLinks_) {
        if (link.active && link.hidCid == hidCid) {
            return &link;
        }
    }
    return nullptr;
}

BluetoothRuntime::ClassicLink*
BluetoothRuntime::findClassicByHandle(std::uint16_t handle) {
    for (ClassicLink& link : classicLinks_) {
        if (link.active && link.connectionHandle == handle) {
            return &link;
        }
    }
    return nullptr;
}

BluetoothRuntime::ClassicLink*
BluetoothRuntime::allocateClassic() {
    for (ClassicLink& link : classicLinks_) {
        if (!link.active) {
            link = {};
            link.active = true;
            return &link;
        }
    }
    return nullptr;
}

BluetoothRuntime::LeLink*
BluetoothRuntime::findLeByHandle(std::uint16_t handle) {
    for (LeLink& link : leLinks_) {
        if (link.active && link.connectionHandle == handle) {
            return &link;
        }
    }
    return nullptr;
}

BluetoothRuntime::LeLink*
BluetoothRuntime::findLeByCid(std::uint16_t hidsCid) {
    for (LeLink& link : leLinks_) {
        if (link.active && link.hidsCid == hidsCid) {
            return &link;
        }
    }
    return nullptr;
}

BluetoothRuntime::LeLink*
BluetoothRuntime::allocateLe() {
    for (LeLink& link : leLinks_) {
        if (!link.active) {
            link = {};
            link.active = true;
            return &link;
        }
    }
    return nullptr;
}

bool BluetoothRuntime::addressAlreadyConnected(
    const std::uint8_t* address
) const {
    if (address == nullptr) {
        return false;
    }

    for (const ClassicLink& link : classicLinks_) {
        if (
            link.active &&
            std::memcmp(
                link.address.data(),
                address,
                link.address.size()
            ) == 0
        ) {
            return true;
        }
    }

    for (const LeLink& link : leLinks_) {
        if (
            link.active &&
            std::memcmp(
                link.address.data(),
                address,
                link.address.size()
            ) == 0
        ) {
            return true;
        }
    }

    return false;
}

void BluetoothRuntime::startClassicInquiry() {
    if (
        !hciWorking_ ||
        !hasFreeConnectionBudget() ||
        classicConnectPending_ ||
        leConnectPending_
    ) {
        discoveryPhase_ = DiscoveryPhase::Idle;
        return;
    }

    gap_stop_scan();
    discoveryPhase_ = DiscoveryPhase::ClassicInquiry;

    // Inquiry length unit is 1.28 s. Four units gives a short discovery slice
    // before OAG rotates to LE scanning.
    gap_inquiry_start(4);
}

void BluetoothRuntime::startLeScan() {
    if (
        !hciWorking_ ||
        !hasFreeConnectionBudget() ||
        classicConnectPending_ ||
        leConnectPending_
    ) {
        discoveryPhase_ = DiscoveryPhase::Idle;
        return;
    }

    gap_set_scan_parameters(0, 48, 48);
    gap_start_scan();

    discoveryPhase_ = DiscoveryPhase::LeScan;
    leScanDeadlineUs_ = time_us_64() + 4000000ull;
}

void BluetoothRuntime::resumeDiscovery() {
    if (!hciWorking_ || !hasFreeConnectionBudget()) {
        discoveryPhase_ = DiscoveryPhase::Idle;
        return;
    }

    if (discoveryPhase_ == DiscoveryPhase::LeScan) {
        startClassicInquiry();
    } else {
        startLeScan();
    }
}

void BluetoothRuntime::startLeHids(
    std::uint16_t connectionHandle
) {
    LeLink* link = findLeByHandle(connectionHandle);
    if (link == nullptr || link->hidsCid != 0) {
        return;
    }

    std::uint16_t cid = 0;
    const std::uint8_t status = hids_client_connect(
        connectionHandle,
        leHidPacketThunk,
        HID_PROTOCOL_MODE_REPORT,
        &cid
    );

    if (status != ERROR_CODE_SUCCESS) {
        gap_disconnect(connectionHandle);
        return;
    }

    link->hidsCid = cid;
}

void BluetoothRuntime::notifyLeDescriptors(LeLink& link) {
    if (observer_ == nullptr || link.hidsCid == 0) {
        return;
    }

    for (
        std::uint8_t service = 0;
        service < link.serviceCount;
        ++service
    ) {
        const std::uint8_t* descriptor =
            hids_client_descriptor_storage_get_descriptor_data(
                link.hidsCid,
                service
            );

        const std::uint16_t descriptorLength =
            hids_client_descriptor_storage_get_descriptor_len(
                link.hidsCid,
                service
            );

        if (descriptor == nullptr || descriptorLength == 0) {
            continue;
        }

        observer_->onBluetoothHidDescriptor(
            TransportType::BluetoothLe,
            link.connectionHandle,
            service,
            descriptor,
            descriptorLength
        );
    }
}

void BluetoothRuntime::disconnectLeServices(LeLink& link) {
    if (observer_ != nullptr) {
        const std::uint8_t count = std::max<std::uint8_t>(
            link.serviceCount,
            1
        );

        for (
            std::uint8_t service = 0;
            service < count;
            ++service
        ) {
            observer_->onBluetoothHidDisconnected(
                TransportType::BluetoothLe,
                link.connectionHandle,
                service
            );
        }
    }

    link = {};
}

void BluetoothRuntime::handleHciPacket(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    (void)channel;
    (void)size;

    if (packetType != HCI_EVENT_PACKET || packet == nullptr) {
        return;
    }

    const std::uint8_t event = hci_event_packet_get_type(packet);

    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (
                btstack_event_state_get_state(packet) ==
                HCI_STATE_WORKING
            ) {
                hciWorking_ = true;
                startClassicInquiry();
            }
            break;

        case GAP_EVENT_INQUIRY_RESULT: {
            if (
                discoveryPhase_ != DiscoveryPhase::ClassicInquiry ||
                classicConnectPending_ ||
                !hasFreeConnectionBudget()
            ) {
                break;
            }

            const std::uint32_t classOfDevice =
                gap_event_inquiry_result_get_class_of_device(packet);

            if (!looksLikeClassicPeripheral(classOfDevice)) {
                break;
            }

            bd_addr_t address {};
            gap_event_inquiry_result_get_bd_addr(packet, address);

            if (addressAlreadyConnected(address)) {
                break;
            }

            gap_inquiry_stop();

            std::uint16_t hidCid = 0;
            const std::uint8_t status = hid_host_connect(
                address,
                HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT,
                &hidCid
            );

            if (status == ERROR_CODE_SUCCESS) {
                classicConnectPending_ = true;
                discoveryPhase_ =
                    DiscoveryPhase::PausedForConnection;
            } else {
                startLeScan();
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (!classicConnectPending_) {
                startLeScan();
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (
                discoveryPhase_ != DiscoveryPhase::LeScan ||
                leConnectPending_ ||
                !hasFreeConnectionBudget()
            ) {
                break;
            }

            const std::uint8_t* adData =
                gap_event_advertising_report_get_data(packet);
            const std::uint8_t adLength =
                gap_event_advertising_report_get_data_length(packet);

            if (!ad_data_contains_uuid16(
                    adLength,
                    adData,
                    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
                )) {
                break;
            }

            bd_addr_t address {};
            gap_event_advertising_report_get_address(
                packet,
                address
            );

            if (addressAlreadyConnected(address)) {
                break;
            }

            const bd_addr_type_t addressType =
                static_cast<bd_addr_type_t>(
                    gap_event_advertising_report_get_address_type(packet)
                );

            gap_stop_scan();

            const std::uint8_t status =
                gap_connect(address, addressType);

            if (status == ERROR_CODE_SUCCESS) {
                leConnectPending_ = true;
                discoveryPhase_ =
                    DiscoveryPhase::PausedForConnection;
            } else {
                startClassicInquiry();
            }
            break;
        }

        case HCI_EVENT_META_GAP:
            if (
                hci_event_gap_meta_get_subevent_code(packet) !=
                GAP_SUBEVENT_LE_CONNECTION_COMPLETE
            ) {
                break;
            } else {
                leConnectPending_ = false;

                const std::uint8_t status =
                    gap_subevent_le_connection_complete_get_status(
                        packet
                    );

                if (status != ERROR_CODE_SUCCESS) {
                    resumeDiscovery();
                    break;
                }

                LeLink* link = allocateLe();
                if (link == nullptr) {
                    gap_disconnect(
                        gap_subevent_le_connection_complete_get_connection_handle(
                            packet
                        )
                    );
                    break;
                }

                link->connectionHandle =
                    gap_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );
                link->addressType =
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
                    address + 6,
                    link->address.begin()
                );

                sm_request_pairing(link->connectionHandle);
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t address {};
            hci_event_pin_code_request_get_bd_addr(
                packet,
                address
            );
            gap_pin_code_response(address, "0000");
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t address {};
            hci_event_user_confirmation_request_get_bd_addr(
                packet,
                address
            );
            gap_ssp_confirmation_response(address);
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            const std::uint16_t handle =
                hci_event_disconnection_complete_get_connection_handle(
                    packet
                );

            if (ClassicLink* classic = findClassicByHandle(handle)) {
                if (observer_ != nullptr) {
                    observer_->onBluetoothHidDisconnected(
                        TransportType::BluetoothClassic,
                        classic->connectionHandle,
                        0
                    );
                }
                *classic = {};
            }

            if (LeLink* le = findLeByHandle(handle)) {
                disconnectLeServices(*le);
            }

            resumeDiscovery();
            break;
        }

        default:
            break;
    }
}

void BluetoothRuntime::handleClassicHidPacket(
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
        hci_event_packet_get_type(packet) != HCI_EVENT_HID_META
    ) {
        return;
    }

    switch (hci_event_hid_meta_get_subevent_code(packet)) {
        case HID_SUBEVENT_INCOMING_CONNECTION:
            // BTstack 2.1.1 emits this subevent only after an incoming HID
            // control link exists; there is no status field in this event.
            // Capacity is the admission gate.
            if (hasFreeConnectionBudget()) {
                hid_host_accept_connection(
                    hid_subevent_incoming_connection_get_hid_cid(
                        packet
                    ),
                    HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT
                );
            } else {
                hid_host_decline_connection(
                    hid_subevent_incoming_connection_get_hid_cid(
                        packet
                    )
                );
            }
            break;

        case HID_SUBEVENT_CONNECTION_OPENED: {
            classicConnectPending_ = false;

            const std::uint8_t status =
                hid_subevent_connection_opened_get_status(packet);

            if (status != ERROR_CODE_SUCCESS) {
                resumeDiscovery();
                break;
            }

            ClassicLink* link = allocateClassic();
            if (link == nullptr) {
                hid_host_disconnect(
                    hid_subevent_connection_opened_get_hid_cid(packet)
                );
                break;
            }

            link->hidCid =
                hid_subevent_connection_opened_get_hid_cid(packet);
            link->connectionHandle =
                hid_subevent_connection_opened_get_con_handle(packet);

            bd_addr_t address {};
            hid_subevent_connection_opened_get_bd_addr(
                packet,
                address
            );
            std::copy(
                address,
                address + 6,
                link->address.begin()
            );

            resumeDiscovery();
            break;
        }

        case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
            if (
                hid_subevent_descriptor_available_get_status(packet) !=
                    ERROR_CODE_SUCCESS ||
                observer_ == nullptr
            ) {
                break;
            }

            const std::uint16_t cid =
                hid_subevent_descriptor_available_get_hid_cid(packet);

            ClassicLink* link = findClassicByCid(cid);
            if (link == nullptr) {
                break;
            }

            const std::uint8_t* descriptor =
                hid_descriptor_storage_get_descriptor_data(cid);
            const std::uint16_t descriptorLength =
                hid_descriptor_storage_get_descriptor_len(cid);

            if (descriptor != nullptr && descriptorLength != 0) {
                observer_->onBluetoothHidDescriptor(
                    TransportType::BluetoothClassic,
                    link->connectionHandle,
                    0,
                    descriptor,
                    descriptorLength
                );
            }
            break;
        }

        case HID_SUBEVENT_REPORT: {
            if (observer_ == nullptr) {
                break;
            }

            const std::uint16_t cid =
                hid_subevent_report_get_hid_cid(packet);
            ClassicLink* link = findClassicByCid(cid);
            if (link == nullptr) {
                break;
            }

            const std::uint8_t* report =
                hid_subevent_report_get_report(packet);
            std::uint16_t reportLength =
                hid_subevent_report_get_report_len(packet);

            // Bluetooth HID interrupt payloads carry a DATA_INPUT (0xA1)
            // transaction prefix. USB-style parsers expect the HID report
            // itself, so normalize it away.
            if (
                report != nullptr &&
                reportLength != 0 &&
                report[0] == 0xA1
            ) {
                ++report;
                --reportLength;
            }

            if (report != nullptr && reportLength != 0) {
                observer_->onBluetoothHidReport(
                    TransportType::BluetoothClassic,
                    link->connectionHandle,
                    0,
                    report,
                    reportLength
                );
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_CLOSED: {
            const std::uint16_t cid =
                hid_subevent_connection_closed_get_hid_cid(packet);

            ClassicLink* link = findClassicByCid(cid);
            if (link != nullptr) {
                if (observer_ != nullptr) {
                    observer_->onBluetoothHidDisconnected(
                        TransportType::BluetoothClassic,
                        link->connectionHandle,
                        0
                    );
                }
                *link = {};
            }

            resumeDiscovery();
            break;
        }

        default:
            break;
    }
}

void BluetoothRuntime::handleSmPacket(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    (void)channel;
    (void)size;

    if (packetType != HCI_EVENT_PACKET || packet == nullptr) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(
                sm_event_just_works_request_get_handle(packet)
            );
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(
                sm_event_numeric_comparison_request_get_handle(packet)
            );
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            const std::uint16_t handle =
                sm_event_pairing_complete_get_handle(packet);
            const std::uint8_t status =
                sm_event_pairing_complete_get_status(packet);

            if (status == ERROR_CODE_SUCCESS) {
                startLeHids(handle);
            } else {
                gap_disconnect(handle);
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            const std::uint16_t handle =
                sm_event_reencryption_complete_get_handle(packet);
            const std::uint8_t status =
                sm_event_reencryption_complete_get_status(packet);

            if (status == ERROR_CODE_SUCCESS) {
                startLeHids(handle);
            } else {
                gap_disconnect(handle);
            }
            break;
        }

        default:
            break;
    }
}

void BluetoothRuntime::handleLeHidPacket(
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
        hci_event_packet_get_type(packet) !=
            HCI_EVENT_GATTSERVICE_META
    ) {
        return;
    }

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            const std::uint16_t cid =
                gattservice_subevent_hid_service_connected_get_hids_cid(
                    packet
                );
            LeLink* link = findLeByCid(cid);
            if (link == nullptr) {
                break;
            }

            const std::uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(
                    packet
                );

            if (status != ERROR_CODE_SUCCESS) {
                gap_disconnect(link->connectionHandle);
                break;
            }

            link->serviceCount = std::min<std::uint8_t>(
                gattservice_subevent_hid_service_connected_get_num_instances(
                    packet
                ),
                static_cast<std::uint8_t>(
                    kMaxHidServicesPerLeDevice
                )
            );

            notifyLeDescriptors(*link);
            resumeDiscovery();
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            if (observer_ == nullptr) {
                break;
            }

            const std::uint16_t cid =
                gattservice_subevent_hid_report_get_hids_cid(packet);
            LeLink* link = findLeByCid(cid);
            if (link == nullptr) {
                break;
            }

            const std::uint8_t service =
                gattservice_subevent_hid_report_get_service_index(
                    packet
                );
            const std::uint8_t reportId =
                gattservice_subevent_hid_report_get_report_id(packet);
            const std::uint8_t* report =
                gattservice_subevent_hid_report_get_report(packet);
            const std::uint16_t reportLength =
                gattservice_subevent_hid_report_get_report_len(packet);

            if (report == nullptr || reportLength == 0) {
                break;
            }

            const std::uint8_t* normalized = report;
            std::size_t normalizedLength = reportLength;

            // HOG exposes report ID separately. Recreate the USB HID wire
            // shape expected by GenericHidGamepadDriver when IDs are used.
            if (reportId != 0) {
                if (
                    static_cast<std::size_t>(reportLength) + 1u >
                    normalizedReport_.size()
                ) {
                    break;
                }

                normalizedReport_[0] = reportId;
                std::memcpy(
                    normalizedReport_.data() + 1,
                    report,
                    reportLength
                );
                normalized = normalizedReport_.data();
                normalizedLength =
                    static_cast<std::size_t>(reportLength) + 1u;
            }

            observer_->onBluetoothHidReport(
                TransportType::BluetoothLe,
                link->connectionHandle,
                service,
                normalized,
                normalizedLength
            );
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED: {
            const std::uint16_t cid =
                gattservice_subevent_hid_service_disconnected_get_hids_cid(
                    packet
                );
            LeLink* link = findLeByCid(cid);
            if (link != nullptr) {
                disconnectLeServices(*link);
                resumeDiscovery();
            }
            break;
        }

        default:
            break;
    }
}

} // namespace oag::firmware
