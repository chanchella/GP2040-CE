#include "oag/firmware/bluetooth_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/gatt-service/hids_client.h"
#include "ble/gatt-service/hids_device.h"

#include "oag_ble_gamepad.h"

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

void peripheralHidPacketThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handlePeripheralHidPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

btstack_packet_callback_registration_t gHciRegistration {};
btstack_packet_callback_registration_t gSmRegistration {};
btstack_timer_source_t gDiscoveryTimer {};
btstack_context_callback_registration_t gPeripheralSendRegistration {};

void discoveryTimerThunk(btstack_timer_source_t* timer) {
    (void)timer;
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handleDiscoveryTimer();
    }
}

void peripheralSendRequestThunk(void* context) {
    (void)context;
    if (gBluetoothRuntime != nullptr) {
        gBluetoothRuntime->handlePeripheralSendRequest();
    }
}

bool looksLikeClassicPeripheral(std::uint32_t classOfDevice) {
    return (classOfDevice & 0x1F00u) == 0x0500u;
}

bool asciiContains(
    const std::uint8_t* data,
    std::uint8_t length,
    const char* needle
) {
    if (data == nullptr || needle == nullptr) return false;
    const std::size_t needleLength = std::strlen(needle);
    if (needleLength == 0 || needleLength > length) return false;

    for (std::uint8_t i = 0;
         static_cast<std::size_t>(i) + needleLength <= length;
         ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needleLength; ++j) {
            char a = static_cast<char>(data[i + j]);
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

const std::uint8_t kBleGamepadReportDescriptor[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x85, 0x01,

    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x10,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x10,
    0x75, 0x01,
    0x81, 0x02,

    0x05, 0x01,
    0x09, 0x39,
    0x15, 0x00,
    0x25, 0x07,
    0x35, 0x00,
    0x46, 0x3B, 0x01,
    0x65, 0x14,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x42,
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x03,

    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x33,
    0x09, 0x34,
    0x16, 0x00, 0x80,
    0x26, 0xFF, 0x7F,
    0x75, 0x10,
    0x95, 0x04,
    0x81, 0x02,

    0x09, 0x32,
    0x09, 0x35,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x02,
    0x81, 0x02,

    0xC0,
};

const std::uint8_t kBlePeripheralAdvertisingData[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x0C, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'O','A','G',' ','G','a','m','e','p','a','d',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF
    ),
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8
    ),
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

bool advertisementLooksLikeHid(const std::uint8_t* packet) {
    const std::uint8_t* data =
        gap_event_advertising_report_get_data(packet);
    const std::uint8_t dataLength =
        gap_event_advertising_report_get_data_length(packet);

    if (ad_data_contains_uuid16(
            dataLength,
            data,
            ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
        )) {
        return true;
    }

    ad_context_t context {};
    for (
        ad_iterator_init(&context, dataLength, data);
        ad_iterator_has_more(&context);
        ad_iterator_next(&context)
    ) {
        const std::uint8_t type = ad_iterator_get_data_type(&context);
        const std::uint8_t len = ad_iterator_get_data_len(&context);
        const std::uint8_t* item = ad_iterator_get_data(&context);

        if (type == BLUETOOTH_DATA_TYPE_APPEARANCE && len >= 2) {
            const std::uint16_t appearance =
                little_endian_read_16(item, 0);
            if (appearance >= 0x03C0 && appearance <= 0x03C4) {
                return true;
            }
        }

        if (
            type != BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME &&
            type != BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME
        ) {
            continue;
        }

        static const char* const kNames[] = {
            "Xbox",
            "Wireless Controller",
            "DualSense",
            "Pro Controller",
            "Joy-Con",
            "Nintendo",
            "8BitDo",
            "GameSir",
            "PXN",
            "MOCUTE",
            "IPEGA",
            "Gamepad",
            "Controller",
        };

        for (const char* name : kNames) {
            if (asciiContains(item, len, name)) {
                return true;
            }
        }
    }

    return false;
}

constexpr std::uint32_t kU8dBondMigrationTag = 0x4F38444Du; // "O8DM"
constexpr std::uint32_t kU8dBondMigrationValue = 0x00080004u;

void clearLeBondDatabase() {
    const int count = le_device_db_max_count();
    for (int i = 0; i < count; ++i) {
        le_device_db_remove(i);
    }
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
    // U8D restores the Golden G2E3 async-context architecture:
    // pico_cyw43_arch_none services BTstack in the SDK background context.
    if (cyw43_arch_init() != PICO_OK) {
        return false;
    }

    observer_ = &observer;
    gBluetoothRuntime = this;

    l2cap_init();

    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    migrateBondStateOnce();

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

    att_server_init(profile_data, nullptr, nullptr);

    hids_device_init(
        0,
        kBleGamepadReportDescriptor,
        static_cast<std::uint16_t>(
            sizeof(kBleGamepadReportDescriptor)
        )
    );
    hids_device_register_packet_handler(
        peripheralHidPacketThunk
    );

    gap_set_local_name("OAG Gamepad");
    gap_connectable_control(1);
    gap_discoverable_control(1);
    configurePeripheralAdvertising();

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

    gPeripheralSendRegistration.callback =
        &peripheralSendRequestThunk;
    gPeripheralSendRegistration.context = nullptr;

    initialized_ = true;
    hci_power_control(HCI_POWER_ON);
    return true;
}

void BluetoothRuntime::migrateBondStateOnce() {
    const btstack_tlv_t* tlv = nullptr;
    void* context = nullptr;
    btstack_tlv_get_instance(&tlv, &context);

    if (tlv == nullptr) {
        return;
    }

    std::uint32_t marker = 0;
    const int markerLength = tlv->get_tag(
        context,
        kU8dBondMigrationTag,
        reinterpret_cast<std::uint8_t*>(&marker),
        sizeof(marker)
    );

    if (
        markerLength == static_cast<int>(sizeof(marker)) &&
        marker == kU8dBondMigrationValue
    ) {
        return;
    }

    // One-time cleanup when entering the Golden-compatible U8D engine.
    // This removes stale U8A/B/C link keys without wiping bonds every boot.
    gap_delete_all_link_keys();
    clearLeBondDatabase();

    marker = kU8dBondMigrationValue;
    tlv->store_tag(
        context,
        kU8dBondMigrationTag,
        reinterpret_cast<const std::uint8_t*>(&marker),
        sizeof(marker)
    );
}

void BluetoothRuntime::configurePeripheralAdvertising() {
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
        static_cast<std::uint8_t>(
            sizeof(kBlePeripheralAdvertisingData)
        ),
        const_cast<std::uint8_t*>(
            kBlePeripheralAdvertisingData
        )
    );

    gap_advertisements_enable(1);
}

void BluetoothRuntime::submitPeripheralGamepad(
    const LogicalGamepadState& state
) {
    peripheralGamepadState_ = state;

    if (!initialized_) {
        return;
    }

    btstack_run_loop_execute_on_main_thread(
        &gPeripheralSendRegistration
    );
}

void BluetoothRuntime::handlePeripheralSendRequest() {
    requestPeripheralSend();
}

void BluetoothRuntime::requestPeripheralSend() {
    if (
        !initialized_ ||
        peripheralConnectionHandle_ == 0xFFFFu ||
        !peripheralSubscribed_ ||
        peripheralSendPending_
    ) {
        return;
    }

    if (
        hids_device_request_can_send_now_event(
            peripheralConnectionHandle_
        ) == ERROR_CODE_SUCCESS
    ) {
        peripheralSendPending_ = true;
    }
}

std::uint8_t BluetoothRuntime::dpadToHat(
    std::uint8_t dpad
) {
    const bool up = (dpad & 0x01u) != 0;
    const bool down = (dpad & 0x02u) != 0;
    const bool left = (dpad & 0x04u) != 0;
    const bool right = (dpad & 0x08u) != 0;

    if (up && right) return 1;
    if (right && down) return 3;
    if (down && left) return 5;
    if (left && up) return 7;
    if (up) return 0;
    if (right) return 2;
    if (down) return 4;
    if (left) return 6;
    return 8;
}

void BluetoothRuntime::sendPeripheralReport() {
    if (
        peripheralConnectionHandle_ == 0xFFFFu ||
        !peripheralSubscribed_
    ) {
        peripheralSendPending_ = false;
        return;
    }

    auto axis16 = [](std::int32_t value) -> std::int16_t {
        if (value == INT32_MIN) return INT16_MIN;
        return static_cast<std::int16_t>(value >> 16);
    };

    const std::uint16_t buttons =
        static_cast<std::uint16_t>(
            peripheralGamepadState_.buttons & 0xFFFFu
        );

    const std::int16_t lx = axis16(peripheralGamepadState_.lx);
    const std::int16_t ly = axis16(peripheralGamepadState_.ly);
    const std::int16_t rx = axis16(peripheralGamepadState_.rx);
    const std::int16_t ry = axis16(peripheralGamepadState_.ry);

    const std::uint8_t report[] = {
        static_cast<std::uint8_t>(buttons & 0xFFu),
        static_cast<std::uint8_t>(buttons >> 8),
        dpadToHat(peripheralGamepadState_.dpad),

        static_cast<std::uint8_t>(static_cast<std::uint16_t>(lx) & 0xFFu),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(lx) >> 8
        ),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(ly) & 0xFFu),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(ly) >> 8
        ),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(rx) & 0xFFu),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(rx) >> 8
        ),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(ry) & 0xFFu),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(ry) >> 8
        ),

        static_cast<std::uint8_t>(
            peripheralGamepadState_.leftTrigger >> 24
        ),
        static_cast<std::uint8_t>(
            peripheralGamepadState_.rightTrigger >> 24
        ),
    };

    hids_device_send_input_report_for_id(
        peripheralConnectionHandle_,
        1,
        report,
        sizeof(report)
    );

    peripheralSendPending_ = false;
}

void BluetoothRuntime::handlePeripheralHidPacket(
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
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
            peripheralConnectionHandle_ =
                hids_subevent_input_report_enable_get_con_handle(
                    packet
                );
            peripheralSubscribed_ =
                hids_subevent_input_report_enable_get_enable(
                    packet
                ) != 0;
            requestPeripheralSend();
            break;

        case HIDS_SUBEVENT_CAN_SEND_NOW:
            sendPeripheralReport();
            break;

        default:
            break;
    }
}

void BluetoothRuntime::poll() {
    if (!initialized_) {
        return;
    }

    // Bluetooth transport/timers are serviced by pico_cyw43_arch_none's
    // async-context, matching the hardware-proven Golden G2E3 firmware.
    serviceDiagnosticLed();
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

void BluetoothRuntime::stopDiscoveryTimer() {
    btstack_run_loop_remove_timer(&gDiscoveryTimer);
}

void BluetoothRuntime::scheduleDiscoveryTimer(
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
    btstack_run_loop_add_timer(&gDiscoveryTimer);
}

void BluetoothRuntime::handleDiscoveryTimer() {
    if (
        !hciWorking_ ||
        !hasFreeConnectionBudget() ||
        classicConnectPending_ ||
        leConnectPending_
    ) {
        return;
    }

    if (discoveryPhase_ == DiscoveryPhase::LeScan) {
        gap_stop_scan();
        startClassicInquiry();
    }
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

    stopDiscoveryTimer();
    gap_stop_scan();
    gap_connect_cancel();

    discoveryPhase_ = DiscoveryPhase::ClassicInquiry;

    // Exact Golden cadence: 4 * 1.28 s ~= 5.1 s.
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

    stopDiscoveryTimer();
    gap_inquiry_stop();
    gap_connect_cancel();

    // Golden G2E3 active scan. Some HID devices expose 0x1812 only in the
    // scan response, so passive scan is insufficient.
    gap_set_scan_parameters(1, 0x0030, 0x0030);
    gap_start_scan();

    discoveryPhase_ = DiscoveryPhase::LeScan;
    scheduleDiscoveryTimer(5000);
}

void BluetoothRuntime::resumeDiscovery() {
    if (!hciWorking_ || !hasFreeConnectionBudget()) {
        stopDiscoveryTimer();
        discoveryPhase_ = DiscoveryPhase::Idle;
        return;
    }

    // Golden always returns to BLE first; the BTstack timer rotates to
    // Classic after five seconds.
    startLeScan();
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

void BluetoothRuntime::serviceDiagnosticLed() {
    if (!hciWorking_) return;

    const std::uint64_t nowUs = time_us_64();

    if (
        activeConnectionCount() != 0 ||
        peripheralConnectionHandle_ != 0xFFFFu
    ) {
        diagnosticLedState_ = true;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        return;
    }

    const std::uint64_t intervalUs =
        (classicConnectPending_ || leConnectPending_)
            ? 120000ull
            : (
                discoveryPhase_ == DiscoveryPhase::ClassicInquiry ||
                discoveryPhase_ == DiscoveryPhase::LeScan
            )
                ? 500000ull
                : 250000ull;

    if (nowUs - diagnosticLastToggleUs_ >= intervalUs) {
        diagnosticLastToggleUs_ = nowUs;
        diagnosticLedState_ = !diagnosticLedState_;
        cyw43_arch_gpio_put(
            CYW43_WL_GPIO_LED_PIN,
            diagnosticLedState_ ? 1 : 0
        );
    }
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
                startLeScan();
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

            stopDiscoveryTimer();
            gap_inquiry_stop();
            discoveryPhase_ = DiscoveryPhase::PausedForConnection;

            std::uint16_t hidCid = 0;
            const std::uint8_t status = hid_host_connect(
                address,
                HID_PROTOCOL_MODE_REPORT,
                &hidCid
            );

            if (status == ERROR_CODE_SUCCESS) {
                classicConnectPending_ = true;
            } else {
                discoveryPhase_ = DiscoveryPhase::Idle;
                startLeScan();
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (
                discoveryPhase_ == DiscoveryPhase::ClassicInquiry &&
                !classicConnectPending_ &&
                hasFreeConnectionBudget()
            ) {
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

            if (!advertisementLooksLikeHid(packet)) {
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

            stopDiscoveryTimer();
            gap_stop_scan();
            discoveryPhase_ = DiscoveryPhase::PausedForConnection;

            const std::uint8_t status =
                gap_connect(address, addressType);

            if (status == ERROR_CODE_SUCCESS) {
                leConnectPending_ = true;
            } else {
                discoveryPhase_ = DiscoveryPhase::Idle;
                startLeScan();
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

                const std::uint16_t connectionHandle =
                    gap_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );

                const std::uint8_t role =
                    gap_subevent_le_connection_complete_get_role(
                        packet
                    );

                if (role == HCI_ROLE_SLAVE) {
                    peripheralConnectionHandle_ = connectionHandle;
                    peripheralSubscribed_ = false;
                    peripheralSendPending_ = false;
                    gap_advertisements_enable(0);
                    break;
                }

                LeLink* link = allocateLe();
                if (link == nullptr) {
                    gap_disconnect(connectionHandle);
                    break;
                }

                link->connectionHandle = connectionHandle;
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
            // Golden G2E3 behavior: do not force a legacy 0000 PIN onto
            // modern HID controllers. Let SSP/HID pairing proceed instead.
            gap_pin_code_negative(address);
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

            if (handle == peripheralConnectionHandle_) {
                peripheralConnectionHandle_ = 0xFFFFu;
                peripheralSubscribed_ = false;
                peripheralSendPending_ = false;
                configurePeripheralAdvertising();
            }

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

            discoveryPhase_ = DiscoveryPhase::Idle;
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
