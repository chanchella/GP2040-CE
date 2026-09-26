#include "oag/firmware/bluetooth_host_v2.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/gatt-service/hids_host.h"

#include "oag_ble_platform.h"

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
constexpr std::uint32_t kU9FreshBondVersion = 3u; // U9D SDK 2.3 / HIDS Host reset
constexpr std::uint32_t kLeScanWindowMs = 5000u;
constexpr std::uint64_t kPairingAssistWindowUs = 30000000ull;

// U10F-PM1-L1: BLE-only latency preference.
// BLE connection interval units are 1.25 ms, so 6..12 requests 7.5..15 ms.
// This is best-effort only: rejection must never disconnect the controller or
// alter pairing/bond semantics.
constexpr std::uint16_t kLeLowLatencyIntervalMin = 6u;
constexpr std::uint16_t kLeLowLatencyIntervalMax = 12u;
constexpr std::uint16_t kLeLowLatencyConnLatency = 0u;
constexpr std::uint16_t kLeLowLatencySupervisionTimeout = 400u; // 4 seconds

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

    // Historical BluetoothHCI installed a local GAP/ATT server before power-on.
    // Keep runtime Host-only: this does not start advertising.
    // UI5K-BT-OUT1 uses one generated ATT database for the existing local GAP
    // service plus the isolated BLE HOG output services. The Host still owns
    // BTstack initialization and all Central-side input clients.
    att_server_init(
        profile_data,
        nullptr,
        nullptr
    );

    hid_host_init(
        classicDescriptorStorage_.data(),
        static_cast<std::uint16_t>(
            classicDescriptorStorage_.size()
        )
    );

    hid_host_register_packet_handler(
        packetThunk
    );

    hids_host_init(
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
    //
    // U9C intentionally performs gap_connect() here, outside the advertising
    // event callback, matching the historical BluetoothHIDMaster flow:
    // scan -> stop -> connect.
    serviceDeferredBleConnect();
    servicePairingAssist();

    // U10A continuous-discovery guard. Normal BLE -> Classic -> BLE cadence
    // remains unchanged; this only recovers an unexpected idle state while
    // peer capacity is still available. At four peers discovery pauses, then
    // resumes automatically after any disconnect.
    if (
        initialized_ &&
        hciWorking_ &&
        !platformOutputLinkActive_ &&
        hasCapacity() &&
        pendingKind_ == PendingKind::None &&
        !deferredBleCandidateValid_ &&
        discoveryPhase_ == DiscoveryPhase::Idle
    ) {
        resumeDiscovery();
    }
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

BluetoothHidOutputResult BluetoothHostV2::sendLeOutputReport(
    std::uint16_t connectionHandle,
    std::uint8_t serviceInstance,
    std::uint8_t reportId,
    const std::uint8_t* report,
    std::size_t reportLength
) {
    if (
        report == nullptr ||
        reportLength == 0 ||
        reportLength > std::numeric_limits<std::uint16_t>::max()
    ) {
        return BluetoothHidOutputResult::Failed;
    }

    Peer* peer =
        findBleByHandle(connectionHandle);

    if (
        peer == nullptr ||
        peer->hidCid == 0 ||
        serviceInstance >= peer->serviceCount
    ) {
        return BluetoothHidOutputResult::Failed;
    }

    // Avoid the TinyUSB/BTstack hid_report_type_t name collision. The BTstack
    // HIDS Host contract uses numeric report type 2 for Output reports; this
    // is the same approach used by the historical hardware-working
    // BluetoothHIDMaster implementation.
    using BtReportType =
        decltype(
            (
                (hids_host_report_t*)
                nullptr
            )->report_type
        );

    const auto outputReportType =
        static_cast<BtReportType>(2);

    const std::uint8_t status =
        hids_host_send_write_report(
            peer->hidCid,
            reportId,
            outputReportType,
            report,
            static_cast<std::uint16_t>(reportLength)
        );

    if (status == ERROR_CODE_SUCCESS) {
        return BluetoothHidOutputResult::Accepted;
    }

    if (status == ERROR_CODE_COMMAND_DISALLOWED) {
        return BluetoothHidOutputResult::Busy;
    }

    return BluetoothHidOutputResult::Failed;
}

bool BluetoothHostV2::hasCapacity() const {
    return connectedPeerCount() < kMaxPeers;
}

bool BluetoothHostV2::isKnownBluetoothCapableUsbGamepad(
    std::uint16_t vid,
    std::uint16_t pid
) {
    // Cable-assist is deliberately allow-listed. A false positive only changes
    // discovery cadence for 30 seconds, but keeping this narrow avoids
    // unnecessary BLE-only windows for ordinary wired controllers.
    if (vid == 0x045E) {
        switch (pid) {
            case 0x02D1: // Xbox One
            case 0x02DD: // Xbox One (2015)
            case 0x02E3: // Elite
            case 0x02EA: // Xbox One S
            case 0x0B00: // Elite Series 2
            case 0x0B0A: // Adaptive Controller
            case 0x0B12: // Xbox Series S|X
                return true;

            default:
                break;
        }
    }

    if (vid == 0x054C) {
        switch (pid) {
            case 0x05C4: // DualShock 4
            case 0x09CC: // DualShock 4 v2
            case 0x0CE6: // DualSense
            case 0x0DF2: // DualSense Edge
                return true;

            default:
                break;
        }
    }

    return vid == 0x057E && pid == 0x2009; // Nintendo Switch Pro
}

void BluetoothHostV2::requestPairingAssist() {
    pairingAssistRequested_ = true;
}

void BluetoothHostV2::notifyWiredGamepadDetached(
    std::uint16_t vid,
    std::uint16_t pid
) {
    if (isKnownBluetoothCapableUsbGamepad(vid, pid)) {
        // Detach refreshes the full window because several controllers do not
        // advertise BLE while their USB data connection is active.
        requestPairingAssist();
    }
}

void BluetoothHostV2::servicePairingAssist() {
    const std::uint64_t nowUs = time_us_64();

    if (
        pairingAssistActive_ &&
        nowUs >= pairingAssistUntilUs_
    ) {
        pairingAssistActive_ = false;
        pairingAssistUntilUs_ = 0;
    }

    if (!pairingAssistRequested_) {
        return;
    }

    if (
        !initialized_ ||
        !hciWorking_ ||
        !hasCapacity()
    ) {
        return;
    }

    // Never disturb a candidate connection, SMP transaction, or HIDS setup.
    // The request stays armed and will be serviced on a later poll.
    if (
        pendingKind_ != PendingKind::None ||
        deferredBleCandidateValid_ ||
        discoveryPhase_ == DiscoveryPhase::PausedForConnection
    ) {
        return;
    }

    pairingAssistRequested_ = false;
    pairingAssistActive_ = true;
    pairingAssistUntilUs_ =
        nowUs + kPairingAssistWindowUs;

    // Restart only discovery. Existing Bluetooth peers are untouched.
    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    gap_connect_cancel();
    discoveryPhase_ = DiscoveryPhase::Idle;

    startLeScan();
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
    if (address == nullptr) {
        return false;
    }

    if (
        pendingKind_ != PendingKind::None &&
        std::memcmp(
            pendingAddress_.data(),
            address,
            pendingAddress_.size()
        ) == 0
    ) {
        return true;
    }

    return
        deferredBleCandidateValid_ &&
        std::memcmp(
            deferredBleAddress_.data(),
            address,
            deferredBleAddress_.size()
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
    deferredBleCandidateValid_ = false;
    discoveryPhase_ = DiscoveryPhase::Idle;
}

void BluetoothHostV2::startLeScan() {
    if (
        !hciWorking_ ||
        platformOutputLinkActive_ ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    stopDiscoveryTimer();
    gap_inquiry_stop();
    gap_connect_cancel();

    deferredBleCandidateValid_ = false;

    // Preserve the hardware-good U10F scan parameters exactly, including
    // passive scanning. Pairing Assist accelerates discovery by staying in BLE
    // instead of alternating into Classic inquiry; it does not increase radio
    // activity while USB Host is timing-sensitive.
    gap_set_scan_params(
        0,
        75,
        50,
        0
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
        platformOutputLinkActive_ ||
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
    if (!hciWorking_ || platformOutputLinkActive_) {
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

void BluetoothHostV2::setPlatformOutputLinkActive(bool active) {
    if (platformOutputLinkActive_ == active) {
        return;
    }

    platformOutputLinkActive_ = active;

    if (active) {
        // Keep already-connected input controllers alive. Only quiesce
        // background discovery while Android/PC completes peripheral-side
        // HID pairing and GATT subscription.
        if (
            pendingKind_ == PendingKind::None &&
            !deferredBleCandidateValid_
        ) {
            stopDiscoveryTimer();
            gap_stop_scan();
            gap_inquiry_stop();
            discoveryPhase_ =
                DiscoveryPhase::PausedForConnection;
        }
        return;
    }

    if (
        pendingKind_ == PendingKind::None &&
        !deferredBleCandidateValid_
    ) {
        discoveryPhase_ = DiscoveryPhase::Idle;
        resumeDiscovery();
    }
}

void BluetoothHostV2::handleDiscoveryTimer() {
    if (
        discoveryPhase_ != DiscoveryPhase::LeScan ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    gap_stop_scan();

    const std::uint64_t nowUs = time_us_64();

    if (
        pairingAssistActive_ &&
        nowUs < pairingAssistUntilUs_
    ) {
        // Pairing Mode is primarily BLE for modern Xbox/PlayStation/Switch
        // pads. Stay BLE-only during the short explicit assist window.
        startLeScan();
        return;
    }

    pairingAssistActive_ = false;
    pairingAssistUntilUs_ = 0;
    startClassicInquiry();
}

bool BluetoothHostV2::advertisementHasHidServiceUuid(
    const std::uint8_t* packet
) const {
    if (packet == nullptr) {
        return false;
    }

    const std::uint8_t* data =
        gap_event_advertising_report_get_data(packet);

    const std::uint8_t dataLength =
        gap_event_advertising_report_get_data_length(packet);

    // Historical BluetoothHIDMaster::connectBLE() scans specifically for
    // service UUID 0x1812. Do not infer HID from appearance in U9C.
    return ad_data_contains_uuid16(
        dataLength,
        data,
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
    );
}

void BluetoothHostV2::serviceDeferredBleConnect() {
    if (
        !deferredBleCandidateValid_ ||
        !hciWorking_ ||
        !hasCapacity() ||
        pendingKind_ != PendingKind::None
    ) {
        return;
    }

    const auto address = deferredBleAddress_;
    const std::uint8_t addressType =
        deferredBleAddressType_;

    deferredBleCandidateValid_ = false;

    // Crucial U9C compatibility rule: stop scanning first, then connect from
    // main-context poll(), never from GAP_EVENT_ADVERTISING_REPORT.
    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    gap_connect_cancel();

    connectLeCandidate(
        address.data(),
        addressType
    );
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
        hids_host_connect(
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

            const std::uint8_t status =
                sm_event_reencryption_complete_get_status(packet);

            if (status == ERROR_CODE_SUCCESS) {
                startLeHids(handle);
                break;
            }

            if (status == ERROR_CODE_PIN_OR_KEY_MISSING) {
                // Follow BTstack's official central recovery path exactly:
                // remote lost/replaced its LTK -> delete only this identity
                // bond, then start fresh SMP on the same live connection.
                // Do not broaden this to generic AUTHENTICATION_FAILURE:
                // U10G showed that over-aggressive recovery can leave a solid
                // ACL link without usable HIDS on real controller hardware.
                bd_addr_t identityAddress {};
                sm_event_reencryption_complete_get_address(
                    packet,
                    identityAddress
                );

                const bd_addr_type_t identityAddressType =
                    static_cast<bd_addr_type_t>(
                        sm_event_reencryption_started_get_addr_type(
                            packet
                        )
                    );

                gap_delete_bonding(
                    identityAddressType,
                    identityAddress
                );

                pairingAssistActive_ = true;
                pairingAssistUntilUs_ =
                    time_us_64() + kPairingAssistWindowUs;

                sm_request_pairing(handle);
                break;
            }

            // For all other errors, preserve U10F's conservative behavior:
            // disconnect and let normal/assist discovery retry later.
            if (findBleByHandle(handle) != nullptr) {
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

    // BTstack 075a078 / HIDS Host calls this handler with two packet_type
    // forms: setup/connection events can arrive as HCI_EVENT_PACKET, while
    // live HID notifications are dispatched as HCI_EVENT_GATTSERVICE_META.
    // The historical BluetoothHIDMaster intentionally ignored packet_type
    // and trusted the meta-event byte in the packet itself. Do the same here
    // so live input reports are not dropped before parsing.
    (void)packetType;

    if (
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

            // U10F-PM1-L1 latency optimization:
            // Once HIDS is fully ready, prefer a lower BLE connection interval
            // only when the current link is slower than 15 ms. The controller
            // is free to reject this request; rejection is deliberately
            // non-fatal so compatibility remains identical to U10F-PM1.
            const std::uint16_t currentInterval =
                gap_le_connection_interval(
                    peer->connectionHandle
                );

            if (
                currentInterval >
                    kLeLowLatencyIntervalMax
            ) {
                (void)gap_request_connection_parameter_update(
                    peer->connectionHandle,
                    kLeLowLatencyIntervalMin,
                    kLeLowLatencyIntervalMax,
                    kLeLowLatencyConnLatency,
                    kLeLowLatencySupervisionTimeout
                );
            }

            // HIDS-ready, not merely a solid controller LED, is the success
            // boundary for Pairing Assist.
            pairingAssistActive_ = false;
            pairingAssistUntilUs_ = 0;

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
                        hids_host_descriptor_storage_get_descriptor_data(
                            peer->hidCid,
                            service
                        );

                    const std::uint16_t descriptorLength =
                        hids_host_descriptor_storage_get_descriptor_len(
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
                hids_host_descriptor_storage_get_descriptor_data(
                    peer->hidCid,
                    service
                );

            const std::uint16_t descriptorLength =
                hids_host_descriptor_storage_get_descriptor_len(
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
                deferredBleCandidateValid_ ||
                !advertisementHasHidServiceUuid(packet)
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

            std::copy(
                address,
                address + deferredBleAddress_.size(),
                deferredBleAddress_.begin()
            );

            deferredBleAddressType_ =
                gap_event_advertising_report_get_address_type(
                    packet
                );

            deferredBleCandidateValid_ = true;

            // Freeze discovery logically. The actual BTstack stop/connect calls
            // are intentionally deferred to poll() / main context.
            discoveryPhase_ =
                DiscoveryPhase::PausedForConnection;
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
                    deferredBleCandidateValid_ = false;
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

                // Peripheral-side LE links are owned by BluetoothPlatformOutput.
                // Do not allocate them as input peers and do not disconnect them.
                if (role == HCI_ROLE_SLAVE) {
                    break;
                }

                if (!hasCapacity()) {
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
