#include "oag/firmware/bluetooth_host_v2.h"

#include <algorithm>
#include <cstring>

#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/gatt-service/hids_client.h"

namespace {

oag::firmware::BluetoothHostV2* gBluetoothHostV2 = nullptr;

btstack_packet_callback_registration_t gHciRegistration {};
btstack_packet_callback_registration_t gSmRegistration {};
btstack_timer_source_t gDiscoveryTimer {};

void packetThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothHostV2 != nullptr) {
        gBluetoothHostV2->handlePacket(
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
    if (gBluetoothHostV2 != nullptr) {
        gBluetoothHostV2->handleSmPacket(
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
    if (gBluetoothHostV2 != nullptr) {
        gBluetoothHostV2->handleLeHidPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

void discoveryTimerThunk(btstack_timer_source_t* timer) {
    (void)timer;

    if (gBluetoothHostV2 != nullptr) {
        gBluetoothHostV2->handleDiscoveryTimer();
    }
}

constexpr std::uint32_t kU9FreshBondTag = 0x4F394252u;
constexpr std::uint32_t kU9FreshBondVersion = 1u;
constexpr std::uint32_t kLeScanWindowMs = 5000u;

} // namespace

namespace oag::firmware {

bool BluetoothHostV2::initialize(
    BluetoothHostV2Observer& observer
) {
    if (initialized_) {
        return true;
    }

    if (cyw43_arch_init() != PICO_OK) {
        return false;
    }

    observer_ = &observer;
    gBluetoothHostV2 = this;

    l2cap_init();
    sm_init();

    sm_set_io_capabilities(
        IO_CAPABILITY_NO_INPUT_NO_OUTPUT
    );

    sm_set_authentication_requirements(
        SM_AUTHREQ_BONDING
    );

    gatt_client_init();

    hid_host_init(
        classicDescriptorStorage_.data(),
        static_cast<std::uint16_t>(
            classicDescriptorStorage_.size()
        )
    );

    hid_host_register_packet_handler(
        packetThunk
    );

    hids_client_init(
        leDescriptorStorage_.data(),
        static_cast<std::uint16_t>(
            leDescriptorStorage_.size()
        )
    );

    gap_set_local_name(
        "OAG Abo Gemi Ultra Gaming"
    );

    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_SNIFF_MODE |
        LM_LINK_POLICY_ENABLE_ROLE_SWITCH
    );

    hci_set_inquiry_mode(
        INQUIRY_MODE_RSSI_AND_EIR
    );

    hci_set_master_slave_policy(
        HCI_ROLE_MASTER
    );

    gHciRegistration.callback = &packetThunk;
    hci_add_event_handler(
        &gHciRegistration
    );

    gSmRegistration.callback = &smPacketThunk;
    sm_add_event_handler(
        &gSmRegistration
    );

    gap_connectable_control(0);
    gap_discoverable_control(0);

    clearLegacyBondsOnce();

    initialized_ = true;
    hci_power_control(HCI_POWER_ON);
    return true;
}

void BluetoothHostV2::poll() {
    // pico_cyw43_arch_none + pico_btstack_cyw43 are serviced by the
    // SDK async context. Deliberately do not call cyw43_arch_poll().
}

std::size_t BluetoothHostV2::connectedPeerCount() const {
    std::size_t count = 0;

    for (const Peer& peer : peers_) {
        if (peer.active) {
            ++count;
        }
    }

    return count;
}

bool BluetoothHostV2::hasCapacity() const {
    return connectedPeerCount() < kMaxPeers;
}

BluetoothHostV2::Peer* BluetoothHostV2::allocatePeer() {
    for (Peer& peer : peers_) {
        if (!peer.active) {
            peer = {};
            peer.active = true;
            return &peer;
        }
    }

    return nullptr;
}

BluetoothHostV2::Peer*
BluetoothHostV2::findBleByHandle(
    std::uint16_t connectionHandle
) {
    for (Peer& peer : peers_) {
        if (
            peer.active &&
            peer.transport == TransportType::BluetoothLe &&
            peer.connectionHandle == connectionHandle
        ) {
            return &peer;
        }
    }

    return nullptr;
}

BluetoothHostV2::Peer*
BluetoothHostV2::findLeByHidsCid(
    std::uint16_t hidsCid
) {
    for (Peer& peer : peers_) {
        if (
            peer.active &&
            peer.transport == TransportType::BluetoothLe &&
            peer.hidCid == hidsCid
        ) {
            return &peer;
        }
    }

    return nullptr;
}

BluetoothHostV2::Peer*
BluetoothHostV2::findClassicByCid(
    std::uint16_t hidCid
) {
    for (Peer& peer : peers_) {
        if (
            peer.active &&
            peer.transport == TransportType::BluetoothClassic &&
            peer.hidCid == hidCid
        ) {
            return &peer;
        }
    }

    return nullptr;
}

bool BluetoothHostV2::addressConnected(
    const std::uint8_t* address
) const {
    if (address == nullptr) {
        return false;
    }

    for (const Peer& peer : peers_) {
        if (
            peer.active &&
            std::memcmp(
                peer.address.data(),
                address,
                peer.address.size()
            ) == 0
        ) {
            return true;
        }
    }

    return false;
}

bool BluetoothHostV2::addressPending(
    const std::uint8_t* address
) const {
    return
        address != nullptr &&
        pendingKind_ != PendingKind::None &&
        std::memcmp(
            pendingAddress_.data(),
            address,
            pendingAddress_.size()
        ) == 0;
}

void BluetoothHostV2::notifyPeerDisconnected(
    Peer& peer
) {
    if (observer_ == nullptr) {
        return;
    }

    const std::uint8_t services =
        std::max<std::uint8_t>(
            peer.serviceCount,
            1
        );

    for (
        std::uint8_t service = 0;
        service < services;
        ++service
    ) {
        observer_->onBluetoothHidDisconnected(
            peer.transport,
            peer.connectionHandle,
            service
        );
    }
}

void BluetoothHostV2::clearPeer(Peer& peer) {
    notifyPeerDisconnected(peer);
    peer = {};
}

void BluetoothHostV2::clearBleBondDatabase() {
    const int count = le_device_db_max_count();

    for (int i = 0; i < count; ++i) {
        le_device_db_remove(i);
    }
}

void BluetoothHostV2::clearLegacyBondsOnce() {
    const btstack_tlv_t* tlv = nullptr;
    void* context = nullptr;
    btstack_tlv_get_instance(
        &tlv,
        &context
    );

    if (tlv == nullptr) {
        return;
    }

    std::uint32_t stored = 0;
    const int length = tlv->get_tag(
        context,
        kU9FreshBondTag,
        reinterpret_cast<std::uint8_t*>(&stored),
        sizeof(stored)
    );

    if (
        length == static_cast<int>(sizeof(stored)) &&
        stored == kU9FreshBondVersion
    ) {
        return;
    }

    gap_delete_all_link_keys();
    clearBleBondDatabase();

    const std::uint32_t marker = kU9FreshBondVersion;
    tlv->store_tag(
        context,
        kU9FreshBondTag,
        reinterpret_cast<const std::uint8_t*>(&marker),
        sizeof(marker)
    );
}

void BluetoothHostV2::stopDiscoveryTimer() {
    btstack_run_loop_remove_timer(
        &gDiscoveryTimer
    );
}

void BluetoothHostV2::scheduleDiscoveryTimer(
    std::uint32_t timeoutMs
) {
    stopDiscoveryTimer();

    btstack_run_loop_set_timer_handler(
        &gDiscoveryTimer,
        discoveryTimerThunk
    );

    btstack_run_loop_set_timer(
        &gDiscoveryTimer,
        timeoutMs
    );

    btstack_run_loop_add_timer(
        &gDiscoveryTimer
    );
}

void BluetoothHostV2::stopDiscovery() {
    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    gap_connect_cancel();
    discoveryPhase_ = DiscoveryPhase::Idle;
}

void BluetoothHostV2::startLeScan() {
    if (
        !hciWorking_ ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    stopDiscoveryTimer();
    gap_inquiry_stop();
    gap_connect_cancel();

    gap_set_scan_parameters(
        1,
        0x0030,
        0x0030
    );

    gap_start_scan();
    discoveryPhase_ = DiscoveryPhase::LeScan;

    scheduleDiscoveryTimer(
        kLeScanWindowMs
    );
}

void BluetoothHostV2::startClassicInquiry() {
    if (
        !hciWorking_ ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    stopDiscoveryTimer();
    gap_stop_scan();
    gap_connect_cancel();

    discoveryPhase_ =
        DiscoveryPhase::ClassicInquiry;

    // Golden G2E3 cadence: 4 * 1.28 s ~= 5.1 s.
    gap_inquiry_start(4);
}

void BluetoothHostV2::resumeDiscovery() {
    if (!hciWorking_) {
        return;
    }

    if (!hasCapacity()) {
        stopDiscovery();
        return;
    }

    if (pendingKind_ != PendingKind::None) {
        return;
    }

    startLeScan();
}

bool BluetoothHostV2::beginDiscovery() {
    if (!initialized_ || !hciWorking_) {
        return false;
    }

    resumeDiscovery();
    return true;
}

void BluetoothHostV2::handleDiscoveryTimer() {
    if (
        discoveryPhase_ != DiscoveryPhase::LeScan ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    gap_stop_scan();
    startClassicInquiry();
}

bool BluetoothHostV2::advertisementLooksLikeHid(
    const std::uint8_t* packet
) const {
    if (packet == nullptr) {
        return false;
    }

    const std::uint8_t* data =
        gap_event_advertising_report_get_data(packet);

    const std::uint8_t dataLength =
        gap_event_advertising_report_get_data_length(packet);

    if (
        ad_data_contains_uuid16(
            dataLength,
            data,
            ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
        )
    ) {
        return true;
    }

    ad_context_t context {};

    for (
        ad_iterator_init(
            &context,
            dataLength,
            data
        );
        ad_iterator_has_more(&context);
        ad_iterator_next(&context)
    ) {
        if (
            ad_iterator_get_data_type(&context) !=
                BLUETOOTH_DATA_TYPE_APPEARANCE ||
            ad_iterator_get_data_len(&context) < 2
        ) {
            continue;
        }

        const std::uint16_t appearance =
            little_endian_read_16(
                ad_iterator_get_data(&context),
                0
            );

        if (
            appearance >= 0x03C0 &&
            appearance <= 0x03C4
        ) {
            return true;
        }
    }

    return false;
}

void BluetoothHostV2::connectLeCandidate(
    const std::uint8_t* address,
    std::uint8_t addressType
) {
    if (
        address == nullptr ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None ||
        addressConnected(address)
    ) {
        return;
    }

    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    gap_connect_cancel();

    std::copy(
        address,
        address + pendingAddress_.size(),
        pendingAddress_.begin()
    );

    pendingAddressType_ = addressType;
    pendingKind_ = PendingKind::Ble;
    discoveryPhase_ =
        DiscoveryPhase::PausedForConnection;

    const std::uint8_t status = gap_connect(
        pendingAddress_.data(),
        static_cast<bd_addr_type_t>(
            pendingAddressType_
        )
    );

    if (status != ERROR_CODE_SUCCESS) {
        pendingKind_ = PendingKind::None;
        resumeDiscovery();
    }
}

void BluetoothHostV2::connectClassicCandidate(
    const std::uint8_t* address
) {
    if (
        address == nullptr ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None ||
        addressConnected(address)
    ) {
        return;
    }

    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    gap_connect_cancel();

    std::copy(
        address,
        address + pendingAddress_.size(),
        pendingAddress_.begin()
    );

    pendingKind_ = PendingKind::Classic;
    discoveryPhase_ =
        DiscoveryPhase::PausedForConnection;

    pendingClassicCid_ = 0;

    const std::uint8_t status =
        hid_host_connect(
            pendingAddress_.data(),
            HID_PROTOCOL_MODE_REPORT,
            &pendingClassicCid_
        );

    if (status != ERROR_CODE_SUCCESS) {
        pendingKind_ = PendingKind::None;
        pendingClassicCid_ = 0;
        resumeDiscovery();
    }
}

void BluetoothHostV2::startLeHids(
    std::uint16_t connectionHandle
) {
    Peer* peer =
        findBleByHandle(connectionHandle);

    if (
        peer == nullptr ||
        peer->hidCid != 0
    ) {
        return;
    }

    std::uint16_t cid = 0;
    const std::uint8_t status =
        hids_client_connect(
            connectionHandle,
            leHidPacketThunk,
            HID_PROTOCOL_MODE_REPORT,
            &cid
        );

    if (status != ERROR_CODE_SUCCESS) {
        gap_disconnect(connectionHandle);
        return;
    }

    peer->hidCid = cid;
}

void BluetoothHostV2::handleSmPacket(
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
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(
                sm_event_just_works_request_get_handle(
                    packet
                )
            );
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(
                sm_event_numeric_comparison_request_get_handle(
                    packet
                )
            );
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            const std::uint16_t handle =
                sm_event_pairing_complete_get_handle(packet);

            if (
                sm_event_pairing_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS
            ) {
                startLeHids(handle);
            } else if (findBleByHandle(handle) != nullptr) {
                gap_disconnect(handle);
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            const std::uint16_t handle =
                sm_event_reencryption_complete_get_handle(packet);

            if (
                sm_event_reencryption_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS
            ) {
                startLeHids(handle);
            } else if (findBleByHandle(handle) != nullptr) {
                gap_disconnect(handle);
            }
            break;
        }

        default:
            break;
    }
}

void BluetoothHostV2::handleLeHidPacket(
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

    switch (
        hci_event_gattservice_meta_get_subevent_code(
            packet
        )
    ) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            const std::uint16_t cid =
                gattservice_subevent_hid_service_connected_get_hids_cid(
                    packet
                );

            Peer* peer = findLeByHidsCid(cid);
            if (peer == nullptr) {
                break;
            }

            const std::uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(
                    packet
                );

            if (status != ERROR_CODE_SUCCESS) {
                gap_disconnect(
                    peer->connectionHandle
                );
                break;
            }

            peer->serviceCount =
                std::min<std::uint8_t>(
                    gattservice_subevent_hid_service_connected_get_num_instances(
                        packet
                    ),
                    static_cast<std::uint8_t>(
                        kMaxServicesPerPeer
                    )
                );

            if (observer_ != nullptr) {
                for (
                    std::uint8_t service = 0;
                    service < peer->serviceCount;
                    ++service
                ) {
                    const std::uint8_t* descriptor =
                        hids_client_descriptor_storage_get_descriptor_data(
                            peer->hidCid,
                            service
                        );

                    const std::uint16_t descriptorLength =
                        hids_client_descriptor_storage_get_descriptor_len(
                            peer->hidCid,
                            service
                        );

                    if (
                        descriptor != nullptr &&
                        descriptorLength != 0
                    ) {
                        observer_->onBluetoothHidReady(
                            TransportType::BluetoothLe,
                            peer->connectionHandle,
                            service,
                            descriptor,
                            descriptorLength
                        );
                    }
                }
            }

            pendingKind_ = PendingKind::None;
            resumeDiscovery();
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            const std::uint16_t cid =
                gattservice_subevent_hid_report_get_hids_cid(
                    packet
                );

            Peer* peer = findLeByHidsCid(cid);
            if (
                peer == nullptr ||
                observer_ == nullptr
            ) {
                break;
            }

            const std::uint8_t service =
                gattservice_subevent_hid_report_get_service_index(
                    packet
                );

            if (service >= peer->serviceCount) {
                break;
            }

            const std::uint8_t* descriptor =
                hids_client_descriptor_storage_get_descriptor_data(
                    peer->hidCid,
                    service
                );

            const std::uint16_t descriptorLength =
                hids_client_descriptor_storage_get_descriptor_len(
                    peer->hidCid,
                    service
                );

            const std::uint8_t* report =
                gattservice_subevent_hid_report_get_report(
                    packet
                );

            const std::uint16_t reportLength =
                gattservice_subevent_hid_report_get_report_len(
                    packet
                );

            if (
                descriptor != nullptr &&
                descriptorLength != 0 &&
                report != nullptr &&
                reportLength != 0
            ) {
                observer_->onBluetoothHidReport(
                    TransportType::BluetoothLe,
                    peer->connectionHandle,
                    service,
                    descriptor,
                    descriptorLength,
                    report,
                    reportLength
                );
            }
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED: {
            const std::uint16_t cid =
                gattservice_subevent_hid_service_disconnected_get_hids_cid(
                    packet
                );

            Peer* peer = findLeByHidsCid(cid);
            if (peer != nullptr) {
                peer->hidCid = 0;
                peer->serviceCount = 0;
                gap_disconnect(
                    peer->connectionHandle
                );
            }
            break;
        }

        default:
            break;
    }
}

void BluetoothHostV2::handlePacket(
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
                pendingKind_ = PendingKind::None;
                startLeScan();
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (
                discoveryPhase_ != DiscoveryPhase::LeScan ||
                pendingKind_ != PendingKind::None ||
                !advertisementLooksLikeHid(packet)
            ) {
                break;
            }

            bd_addr_t address {};
            gap_event_advertising_report_get_address(
                packet,
                address
            );

            if (
                addressConnected(address) ||
                addressPending(address)
            ) {
                break;
            }

            connectLeCandidate(
                address,
                gap_event_advertising_report_get_address_type(
                    packet
                )
            );
            break;
        }

        case GAP_EVENT_INQUIRY_RESULT: {
            if (
                discoveryPhase_ !=
                    DiscoveryPhase::ClassicInquiry ||
                pendingKind_ != PendingKind::None
            ) {
                break;
            }

            const std::uint32_t classOfDevice =
                gap_event_inquiry_result_get_class_of_device(
                    packet
                );

            if ((classOfDevice & 0x1F00u) != 0x0500u) {
                break;
            }

            bd_addr_t address {};
            gap_event_inquiry_result_get_bd_addr(
                packet,
                address
            );

            if (
                addressConnected(address) ||
                addressPending(address)
            ) {
                break;
            }

            connectClassicCandidate(address);
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (
                discoveryPhase_ ==
                    DiscoveryPhase::ClassicInquiry &&
                pendingKind_ == PendingKind::None
            ) {
                startLeScan();
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
                    pendingKind_ = PendingKind::None;
                    resumeDiscovery();
                    break;
                }

                const std::uint16_t connectionHandle =
                    gap_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );

                const std::uint8_t role =
                    gap_subevent_le_connection_complete_get_role(
                        packet
                    );

                if (
                    role == HCI_ROLE_SLAVE ||
                    !hasCapacity()
                ) {
                    gap_disconnect(connectionHandle);
                    pendingKind_ = PendingKind::None;
                    resumeDiscovery();
                    break;
                }

                Peer* peer = allocatePeer();
                if (peer == nullptr) {
                    gap_disconnect(connectionHandle);
                    pendingKind_ = PendingKind::None;
                    resumeDiscovery();
                    break;
                }

                peer->transport =
                    TransportType::BluetoothLe;
                peer->connectionHandle =
                    connectionHandle;
                peer->addressType =
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
                    address + peer->address.size(),
                    peer->address.begin()
                );

                pendingKind_ = PendingKind::None;
                discoveryPhase_ =
                    DiscoveryPhase::PausedForConnection;

                sm_request_pairing(
                    connectionHandle
                );
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t address {};
            hci_event_pin_code_request_get_bd_addr(
                packet,
                address
            );

            gap_pin_code_negative(address);
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t address {};
            hci_event_user_confirmation_request_get_bd_addr(
                packet,
                address
            );

            gap_ssp_confirmation_response(
                address
            );
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            const std::uint16_t handle =
                hci_event_disconnection_complete_get_connection_handle(
                    packet
                );

            Peer* peer =
                findBleByHandle(handle);

            if (peer != nullptr) {
                clearPeer(*peer);
            }

            pendingKind_ = PendingKind::None;
            resumeDiscovery();
            break;
        }

        case HCI_EVENT_HID_META: {
            const std::uint8_t subevent =
                hci_event_hid_meta_get_subevent_code(
                    packet
                );

            switch (subevent) {
                case HID_SUBEVENT_INCOMING_CONNECTION:
                    if (hasCapacity()) {
                        hid_host_accept_connection(
                            hid_subevent_incoming_connection_get_hid_cid(
                                packet
                            ),
                            HID_PROTOCOL_MODE_REPORT
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
                    const std::uint8_t status =
                        hid_subevent_connection_opened_get_status(
                            packet
                        );

                    if (status != ERROR_CODE_SUCCESS) {
                        pendingKind_ = PendingKind::None;
                        pendingClassicCid_ = 0;
                        resumeDiscovery();
                        break;
                    }

                    if (!hasCapacity()) {
                        hid_host_disconnect(
                            hid_subevent_connection_opened_get_hid_cid(
                                packet
                            )
                        );
                        pendingKind_ = PendingKind::None;
                        resumeDiscovery();
                        break;
                    }

                    Peer* peer = allocatePeer();
                    if (peer == nullptr) {
                        hid_host_disconnect(
                            hid_subevent_connection_opened_get_hid_cid(
                                packet
                            )
                        );
                        pendingKind_ = PendingKind::None;
                        resumeDiscovery();
                        break;
                    }

                    peer->transport =
                        TransportType::BluetoothClassic;
                    peer->hidCid =
                        hid_subevent_connection_opened_get_hid_cid(
                            packet
                        );
                    peer->connectionHandle =
                        hid_subevent_connection_opened_get_con_handle(
                            packet
                        );
                    peer->serviceCount = 1;

                    bd_addr_t address {};
                    hid_subevent_connection_opened_get_bd_addr(
                        packet,
                        address
                    );

                    std::copy(
                        address,
                        address + peer->address.size(),
                        peer->address.begin()
                    );

                    pendingKind_ = PendingKind::None;
                    pendingClassicCid_ = 0;
                    break;
                }

                case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
                    const std::uint16_t cid =
                        hid_subevent_descriptor_available_get_hid_cid(
                            packet
                        );

                    Peer* peer =
                        findClassicByCid(cid);

                    if (peer == nullptr) {
                        break;
                    }

                    if (
                        hid_subevent_descriptor_available_get_status(
                            packet
                        ) != ERROR_CODE_SUCCESS
                    ) {
                        hid_host_disconnect(cid);
                        break;
                    }

                    const std::uint8_t* descriptor =
                        hid_descriptor_storage_get_descriptor_data(
                            cid
                        );

                    const std::uint16_t descriptorLength =
                        hid_descriptor_storage_get_descriptor_len(
                            cid
                        );

                    if (
                        observer_ != nullptr &&
                        descriptor != nullptr &&
                        descriptorLength != 0
                    ) {
                        observer_->onBluetoothHidReady(
                            TransportType::BluetoothClassic,
                            peer->connectionHandle,
                            0,
                            descriptor,
                            descriptorLength
                        );
                    }

                    resumeDiscovery();
                    break;
                }

                case HID_SUBEVENT_REPORT: {
                    const std::uint16_t cid =
                        hid_subevent_report_get_hid_cid(
                            packet
                        );

                    Peer* peer =
                        findClassicByCid(cid);

                    if (
                        peer == nullptr ||
                        observer_ == nullptr
                    ) {
                        break;
                    }

                    const std::uint8_t* descriptor =
                        hid_descriptor_storage_get_descriptor_data(
                            cid
                        );

                    const std::uint16_t descriptorLength =
                        hid_descriptor_storage_get_descriptor_len(
                            cid
                        );

                    const std::uint8_t* report =
                        hid_subevent_report_get_report(
                            packet
                        );

                    std::uint16_t reportLength =
                        hid_subevent_report_get_report_len(
                            packet
                        );

                    if (
                        report != nullptr &&
                        reportLength != 0 &&
                        report[0] == 0xA1
                    ) {
                        ++report;
                        --reportLength;
                    }

                    if (
                        descriptor != nullptr &&
                        descriptorLength != 0 &&
                        report != nullptr &&
                        reportLength != 0
                    ) {
                        observer_->onBluetoothHidReport(
                            TransportType::BluetoothClassic,
                            peer->connectionHandle,
                            0,
                            descriptor,
                            descriptorLength,
                            report,
                            reportLength
                        );
                    }
                    break;
                }

                case HID_SUBEVENT_CONNECTION_CLOSED: {
                    const std::uint16_t cid =
                        hid_subevent_connection_closed_get_hid_cid(
                            packet
                        );

                    Peer* peer =
                        findClassicByCid(cid);

                    if (peer != nullptr) {
                        clearPeer(*peer);
                    }

                    pendingKind_ = PendingKind::None;
                    pendingClassicCid_ = 0;
                    resumeDiscovery();
                    break;
                }

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

} // namespace oag::firmware
