#include "addons/universal_bluetooth_host.h"

#if OAG_BLUETOOTH_HOST_ENABLED && OAG_BLUETOOTH_PLATFORM_SUPPORTED

#include <cstring>

#define HID_REPORT_TYPE_INPUT    BT_HID_REPORT_TYPE_INPUT
#define HID_REPORT_TYPE_OUTPUT   BT_HID_REPORT_TYPE_OUTPUT
#define HID_REPORT_TYPE_FEATURE  BT_HID_REPORT_TYPE_FEATURE
#define hid_report_type_t        bt_hid_report_type_t

#include "btstack.h"

#undef HID_REPORT_TYPE_INPUT
#undef HID_REPORT_TYPE_OUTPUT
#undef HID_REPORT_TYPE_FEATURE
#undef hid_report_type_t
#include "btstack_tlv.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "device/oag_identity.h"
#include "input/universal_input_manager.h"
#include "input/universal_human_interface_manager.h"
#include "output/universal_feedback_manager.h"

namespace {

static constexpr uint32_t OAG_BLE_REMOTE_TLV_TAG = 0x4F414742u; // "OAGB"
static constexpr uint32_t OAG_CLASSIC_REMOTE_TLV_TAG = 0x4F414743u; // "OAGC"
static constexpr uint32_t BLE_DISCOVERY_WINDOW_MS = 5000;
static constexpr uint8_t MAX_HID_SERVICES = 4;
static constexpr uint8_t MAX_FIELD_RANGES = 96;
static constexpr uint8_t MAX_HID_REPORT_META = 16;
static constexpr uint8_t MAX_HID_REPORT_CONTRIBUTIONS = 8;

static constexpr uint16_t USAGE_PAGE_GENERIC_DESKTOP = 0x01;
static constexpr uint16_t USAGE_PAGE_SIMULATION = 0x02;
static constexpr uint16_t USAGE_PAGE_KEYBOARD = 0x07;
static constexpr uint16_t USAGE_PAGE_BUTTON = 0x09;
static constexpr uint16_t USAGE_PAGE_CONSUMER = 0x0C;

static constexpr uint16_t USAGE_X = 0x30;
static constexpr uint16_t USAGE_Y = 0x31;
static constexpr uint16_t USAGE_Z = 0x32;
static constexpr uint16_t USAGE_RX = 0x33;
static constexpr uint16_t USAGE_RY = 0x34;
static constexpr uint16_t USAGE_RZ = 0x35;
static constexpr uint16_t USAGE_WHEEL = 0x38;
static constexpr uint16_t USAGE_HAT = 0x39;
static constexpr uint16_t USAGE_DPAD_UP = 0x90;
static constexpr uint16_t USAGE_DPAD_DOWN = 0x91;
static constexpr uint16_t USAGE_DPAD_RIGHT = 0x92;
static constexpr uint16_t USAGE_DPAD_LEFT = 0x93;

static constexpr uint16_t USAGE_SIM_ACCELERATOR = 0xC4;
static constexpr uint16_t USAGE_SIM_BRAKE = 0xC5;
static constexpr uint16_t USAGE_CONSUMER_AC_PAN = 0x0238;

static constexpr uint16_t USAGE_DESKTOP_MOUSE = 0x02;
static constexpr uint16_t USAGE_DESKTOP_JOYSTICK = 0x04;
static constexpr uint16_t USAGE_DESKTOP_GAMEPAD = 0x05;
static constexpr uint16_t USAGE_DESKTOP_KEYBOARD = 0x06;

enum HidReportClassFlag : uint8_t {
    HID_REPORT_CLASS_NONE = 0,
    HID_REPORT_CLASS_KEYBOARD = 1u << 0,
    HID_REPORT_CLASS_CONSUMER = 1u << 1,
    HID_REPORT_CLASS_BUTTONS = 1u << 2,
    HID_REPORT_CLASS_POINTER = 1u << 3,
};

enum class BluetoothGamepadProfile : uint8_t {
    GENERIC_HID = 0,
    XBOX_BLE,
    SONY_DS4_CLASSIC,
    SONY_DUALSENSE_CLASSIC,
    NINTENDO_SWITCH_CLASSIC,
};

struct StoredBleRemote {
    bd_addr_t address {};
    uint8_t addressType = 0;
};

struct StoredClassicRemote {
    bd_addr_t address {};
    uint8_t profile = static_cast<uint8_t>(
        BluetoothGamepadProfile::GENERIC_HID
    );
};

struct HidFieldRange {
    uint16_t reportId = HID_REPORT_ID_UNDEFINED;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
};

struct HidReportMeta {
    bool used = false;
    uint16_t reportId = HID_REPORT_ID_UNDEFINED;
    uint8_t classFlags = HID_REPORT_CLASS_NONE;
};

struct KeyboardReportContribution {
    bool used = false;
    uint16_t reportId = HID_REPORT_ID_UNDEFINED;
    UniversalKeyboardState state {};
};

struct MouseReportContribution {
    bool used = false;
    uint16_t reportId = HID_REPORT_ID_UNDEFINED;
    uint32_t buttons = 0;
};

struct HidServiceCache {
    bool parsed = false;

    bool hasGamepadApplication = false;
    bool hasKeyboardApplication = false;
    bool hasMouseApplication = false;

    bool looksLikeGamepad = false;
    bool looksLikeKeyboard = false;
    bool looksLikeMouse = false;

    bool xboxBleButtonLayout = false;
    BluetoothGamepadProfile profile = BluetoothGamepadProfile::GENERIC_HID;

    uint8_t fieldCount = 0;
    HidFieldRange fields[MAX_FIELD_RANGES] {};

    uint8_t reportMetaCount = 0;
    HidReportMeta reportMeta[MAX_HID_REPORT_META] {};

    KeyboardReportContribution
        keyboardReports[MAX_HID_REPORT_CONTRIBUTIONS] {};
    MouseReportContribution
        mouseReports[MAX_HID_REPORT_CONTRIBUTIONS] {};
};

enum class BleHostState : uint8_t {
    WAITING_HCI = 0,
    SCANNING,
    CONNECTING,
    PAIRING,
    HIDS_CONNECTING,
    READY,
};

enum class DiscoveryMode : uint8_t {
    NONE = 0,
    BLE,
    CLASSIC,
};

static btstack_packet_callback_registration_t hciEventCallback {};
static btstack_packet_callback_registration_t smEventCallback {};

static uint8_t classicDescriptorStorage[1024] {};
static uint8_t leDescriptorStorage[2048] {};

static BleHostState bleState = BleHostState::WAITING_HCI;
static DiscoveryMode discoveryMode = DiscoveryMode::NONE;
static btstack_timer_source_t discoveryTimer {};

static StoredBleRemote remoteDevice {};
static bool remoteKnown = false;
static bool remotePersisted = false;

static StoredClassicRemote classicRemote {};
static bool classicRemoteKnown = false;
static bool classicRemotePersisted = false;

static hci_con_handle_t connectionHandle = HCI_CON_HANDLE_INVALID;
static uint16_t hidsCid = 0;
static uint8_t hidsServiceCount = 0;

static uint16_t classicHidCid = 0;
static bool classicConnecting = false;
static bool classicDescriptorAvailable = false;
static HidServiceCache classicCache {};

static HidServiceCache serviceCaches[MAX_HID_SERVICES] {};

static GamepadState bluetoothGamepadState {};
static bool bluetoothGamepadStateValid = false;
static bool bluetoothSlotConnected = false;
static BluetoothGamepadProfile activeBluetoothProfile =
    BluetoothGamepadProfile::GENERIC_HID;

static uint32_t lastBluetoothFeedbackGeneration = 0;
static uint64_t lastBluetoothFeedbackAttemptUs = 0;
static uint8_t dualsenseOutputSequence = 0;
static uint8_t switchOutputPacketNumber = 0;

static uint64_t diagnosticLastToggleUs = 0;
static bool diagnosticLedState = false;

static void serviceBluetoothDiagnosticLed() {
    uint64_t now = time_us_64();
    uint64_t intervalUs = 0;

    switch (bleState) {
        case BleHostState::SCANNING:
            intervalUs = 500000;
            break;

        case BleHostState::CONNECTING:
        case BleHostState::PAIRING:
        case BleHostState::HIDS_CONNECTING:
            intervalUs = 120000;
            break;

        case BleHostState::READY:
            diagnosticLedState = true;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            return;

        case BleHostState::WAITING_HCI:
            intervalUs = 250000;
            break;

        default:
            diagnosticLedState = false;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            return;
    }

    if (now - diagnosticLastToggleUs >= intervalUs) {
        diagnosticLastToggleUs = now;
        diagnosticLedState = !diagnosticLedState;
        cyw43_arch_gpio_put(
            CYW43_WL_GPIO_LED_PIN,
            diagnosticLedState ? 1 : 0
        );
    }
}

static const btstack_tlv_t* tlvImpl = nullptr;
static void* tlvContext = nullptr;

static void clearBleBondDatabase() {
    const int maxEntries = le_device_db_max_count();

    for (int i = 0; i < maxEntries; i++) {
        le_device_db_remove(i);
    }
}

static bool asciiContains(
    const uint8_t* data,
    uint8_t length,
    const char* needle
) {
    if (data == nullptr || needle == nullptr) return false;

    const size_t needleLength = std::strlen(needle);
    if (needleLength == 0 || needleLength > length) return false;

    for (uint8_t i = 0; i + needleLength <= length; i++) {
        bool match = true;

        for (size_t j = 0; j < needleLength; j++) {
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

static BluetoothGamepadProfile classifyClassicInquiry(
    const uint8_t* packet
) {
    if (gap_event_inquiry_result_get_device_id_available(packet)) {
        const uint16_t vid =
            gap_event_inquiry_result_get_device_id_vendor_id(packet);
        const uint16_t pid =
            gap_event_inquiry_result_get_device_id_product_id(packet);

        if (vid == 0x054C) {
            if (pid == 0x05C4 || pid == 0x09CC) {
                return BluetoothGamepadProfile::SONY_DS4_CLASSIC;
            }

            if (pid == 0x0CE6 || pid == 0x0DF2) {
                return BluetoothGamepadProfile::SONY_DUALSENSE_CLASSIC;
            }
        }

        if (
            vid == 0x057E &&
            (
                pid == 0x2006 || // Joy-Con L
                pid == 0x2007 || // Joy-Con R
                pid == 0x2009    // Pro Controller
            )
        ) {
            return BluetoothGamepadProfile::NINTENDO_SWITCH_CLASSIC;
        }
    }

    if (gap_event_inquiry_result_get_name_available(packet)) {
        const uint8_t nameLength =
            gap_event_inquiry_result_get_name_len(packet);
        const uint8_t* name =
            gap_event_inquiry_result_get_name(packet);

        if (
            asciiContains(name, nameLength, "DualSense") ||
            asciiContains(name, nameLength, "DualSense Wireless")
        ) {
            return BluetoothGamepadProfile::SONY_DUALSENSE_CLASSIC;
        }

        if (
            asciiContains(name, nameLength, "Pro Controller") ||
            asciiContains(name, nameLength, "Joy-Con") ||
            asciiContains(name, nameLength, "Nintendo")
        ) {
            return BluetoothGamepadProfile::NINTENDO_SWITCH_CLASSIC;
        }
    }

    return BluetoothGamepadProfile::GENERIC_HID;
}

static void startBleScan();
static void startClassicInquiry();
static void connectStoredRemote();
static void connectStoredClassic();
static void connectHids();
static void discoveryTimerHandler(btstack_timer_source_t* timer);
static void handleGattClientEvent(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
    uint16_t size
);

static void resetBluetoothHumanInterfaceState() {
    UHIDINPUT.disconnectKeyboard(UNIVERSAL_HID_SLOT_BLUETOOTH);
    UHIDINPUT.disconnectMouse(UNIVERSAL_HID_SLOT_BLUETOOTH);
}

static void resetBluetoothGamepadState() {
    bluetoothGamepadState = GamepadState {};
    bluetoothGamepadStateValid = false;
    activeBluetoothProfile = BluetoothGamepadProfile::GENERIC_HID;
    lastBluetoothFeedbackGeneration = 0;
    lastBluetoothFeedbackAttemptUs = 0;
    dualsenseOutputSequence = 0;
    switchOutputPacketNumber = 0;

    if (bluetoothSlotConnected) {
        UINPUT.disconnect(UNIVERSAL_INPUT_SLOT_BLUETOOTH);
        bluetoothSlotConnected = false;
    }
}

static void resetServiceCaches() {
    for (uint8_t i = 0; i < MAX_HID_SERVICES; i++) {
        serviceCaches[i] = HidServiceCache {};
    }

    classicCache = HidServiceCache {};
    classicDescriptorAvailable = false;
}

static bool loadStoredRemote() {
    btstack_tlv_get_instance(&tlvImpl, &tlvContext);

    if (tlvImpl == nullptr) {
        return false;
    }

    StoredBleRemote stored {};

    const int len = tlvImpl->get_tag(
        tlvContext,
        OAG_BLE_REMOTE_TLV_TAG,
        reinterpret_cast<uint8_t*>(&stored),
        sizeof(stored)
    );

    if (len != static_cast<int>(sizeof(stored))) {
        return false;
    }

    remoteDevice = stored;
    remoteKnown = true;
    remotePersisted = true;
    return true;
}

static void saveStoredRemote() {
    if (remotePersisted) {
        return;
    }

    if (tlvImpl == nullptr) {
        btstack_tlv_get_instance(&tlvImpl, &tlvContext);
    }

    if (tlvImpl == nullptr) {
        return;
    }

    const int result = tlvImpl->store_tag(
        tlvContext,
        OAG_BLE_REMOTE_TLV_TAG,
        reinterpret_cast<uint8_t const*>(&remoteDevice),
        sizeof(remoteDevice)
    );

    if (result == 0) {
        remotePersisted = true;
    }
}


static bool loadStoredClassicRemote() {
    if (tlvImpl == nullptr) {
        btstack_tlv_get_instance(&tlvImpl, &tlvContext);
    }

    if (tlvImpl == nullptr) {
        return false;
    }

    StoredClassicRemote stored {};
    const int len = tlvImpl->get_tag(
        tlvContext,
        OAG_CLASSIC_REMOTE_TLV_TAG,
        reinterpret_cast<uint8_t*>(&stored),
        sizeof(stored)
    );

    if (len != static_cast<int>(sizeof(stored))) {
        return false;
    }

    classicRemote = stored;
    classicRemoteKnown = true;
    classicRemotePersisted = true;
    return true;
}

static void saveStoredClassicRemote() {
    if (classicRemotePersisted) {
        return;
    }

    if (tlvImpl == nullptr) {
        btstack_tlv_get_instance(&tlvImpl, &tlvContext);
    }

    if (tlvImpl == nullptr) {
        return;
    }

    const int result = tlvImpl->store_tag(
        tlvContext,
        OAG_CLASSIC_REMOTE_TLV_TAG,
        reinterpret_cast<uint8_t const*>(&classicRemote),
        sizeof(classicRemote)
    );

    if (result == 0) {
        classicRemotePersisted = true;
    }
}

static bool advertisementLooksLikeHid(uint8_t const* packet) {
    const uint8_t* data =
        gap_event_advertising_report_get_data(packet);

    const uint8_t dataLength =
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
                BLUETOOTH_DATA_TYPE_APPEARANCE
        ) {
            continue;
        }

        if (ad_iterator_get_data_len(&context) < 2) {
            continue;
        }

        const uint8_t* appearanceData =
            ad_iterator_get_data(&context);

        const uint16_t appearance =
            little_endian_read_16(
                appearanceData,
                0
            );

        // Bluetooth SIG HID appearances:
        // 0x03C0 generic HID, 0x03C1 keyboard, 0x03C2 mouse,
        // 0x03C3 joystick, 0x03C4 gamepad.
        if (
            appearance >= 0x03C0 &&
            appearance <= 0x03C4
        ) {
            return true;
        }
    }

    return false;
}

static void stopDiscoveryTimer() {
    btstack_run_loop_remove_timer(&discoveryTimer);
}

static void scheduleDiscoveryTimer(uint32_t timeoutMs) {
    stopDiscoveryTimer();
    btstack_run_loop_set_timer_handler(
        &discoveryTimer,
        discoveryTimerHandler
    );
    btstack_run_loop_set_timer(
        &discoveryTimer,
        timeoutMs
    );
    btstack_run_loop_add_timer(
        &discoveryTimer
    );
}

static void startBleScan() {
    if (
        discoveryMode == DiscoveryMode::BLE &&
        bleState == BleHostState::SCANNING
    ) {
        return;
    }

    gap_inquiry_stop();
    gap_connect_cancel();

    // Active scan is required for HID devices that expose UUID 0x1812
    // only in the scan response rather than the primary advertisement.
    gap_set_scan_parameters(
        1,
        0x0030,
        0x0030
    );

    gap_start_scan();
    discoveryMode = DiscoveryMode::BLE;
    bleState = BleHostState::SCANNING;

    scheduleDiscoveryTimer(
        BLE_DISCOVERY_WINDOW_MS
    );
}

static void startClassicInquiry() {
    stopDiscoveryTimer();

    gap_stop_scan();
    gap_connect_cancel();

    discoveryMode = DiscoveryMode::CLASSIC;
    bleState = BleHostState::WAITING_HCI;

    // 4 * 1.28 s ~= 5.1 s, matching the proven legacy scan cadence.
    gap_inquiry_start(4);
}

static void discoveryTimerHandler(btstack_timer_source_t* timer) {
    (void)timer;

    if (
        bluetoothSlotConnected ||
        connectionHandle != HCI_CON_HANDLE_INVALID ||
        classicConnecting ||
        classicHidCid != 0
    ) {
        return;
    }

    if (discoveryMode == DiscoveryMode::BLE) {
        gap_stop_scan();
        startClassicInquiry();
    }
}

static void connectStoredRemote() {
    if (!remoteKnown) {
        startBleScan();
        return;
    }

    stopDiscoveryTimer();
    gap_stop_scan();
    discoveryMode = DiscoveryMode::NONE;

    const uint8_t status = gap_connect(
        remoteDevice.address,
        static_cast<bd_addr_type_t>(remoteDevice.addressType)
    );

    if (status == ERROR_CODE_SUCCESS) {
        bleState = BleHostState::CONNECTING;
        return;
    }

    startBleScan();
}


static void connectStoredClassic() {
    if (!classicRemoteKnown) {
        startBleScan();
        return;
    }

    stopDiscoveryTimer();
    gap_stop_scan();
    gap_inquiry_stop();
    discoveryMode = DiscoveryMode::NONE;

    classicConnecting = true;

    const uint8_t status = hid_host_connect(
        classicRemote.address,
        HID_PROTOCOL_MODE_REPORT,
        &classicHidCid
    );

    if (status != ERROR_CODE_SUCCESS) {
        classicConnecting = false;
        classicHidCid = 0;
        startBleScan();
    }
}

static void connectHids() {
    if (
        connectionHandle == HCI_CON_HANDLE_INVALID ||
        bleState == BleHostState::HIDS_CONNECTING ||
        bleState == BleHostState::READY
    ) {
        return;
    }

    const uint8_t status = hids_client_connect(
        connectionHandle,
        handleGattClientEvent,
        HID_PROTOCOL_MODE_REPORT,
        &hidsCid
    );

    if (status == ERROR_CODE_SUCCESS) {
        bleState = BleHostState::HIDS_CONNECTING;
        return;
    }

    gap_disconnect(connectionHandle);
}

static HidReportMeta* ensureReportMeta(
    HidServiceCache& cache,
    uint16_t reportId
) {
    for (uint8_t i = 0; i < cache.reportMetaCount; i++) {
        if (
            cache.reportMeta[i].used &&
            cache.reportMeta[i].reportId == reportId
        ) {
            return &cache.reportMeta[i];
        }
    }

    if (cache.reportMetaCount >= MAX_HID_REPORT_META) {
        return nullptr;
    }

    HidReportMeta& meta =
        cache.reportMeta[cache.reportMetaCount++];

    meta.used = true;
    meta.reportId = reportId;
    return &meta;
}

static const HidReportMeta* findReportMeta(
    HidServiceCache const& cache,
    uint16_t reportId
) {
    for (uint8_t i = 0; i < cache.reportMetaCount; i++) {
        if (
            cache.reportMeta[i].used &&
            (
                cache.reportMeta[i].reportId == reportId ||
                cache.reportMeta[i].reportId == HID_REPORT_ID_UNDEFINED
            )
        ) {
            return &cache.reportMeta[i];
        }
    }

    return nullptr;
}

static void classifyTopLevelApplications(
    HidServiceCache& cache,
    const uint8_t* descriptor,
    uint16_t descriptorLength
) {
    uint32_t usagePage = 0;
    uint32_t localUsage = 0;

    uint16_t offset = 0;

    while (offset < descriptorLength) {
        const uint8_t prefix = descriptor[offset++];

        if (prefix == 0xFE) {
            if (offset + 2 > descriptorLength) {
                break;
            }

            const uint8_t longSize = descriptor[offset];
            offset = static_cast<uint16_t>(
                offset + 2 + longSize
            );
            continue;
        }

        uint8_t dataSize = prefix & 0x03;
        if (dataSize == 3) {
            dataSize = 4;
        }

        if (offset + dataSize > descriptorLength) {
            break;
        }

        uint32_t value = 0;

        for (uint8_t i = 0; i < dataSize; i++) {
            value |=
                static_cast<uint32_t>(descriptor[offset + i]) <<
                (8 * i);
        }

        offset = static_cast<uint16_t>(offset + dataSize);

        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = (prefix >> 4) & 0x0F;

        // Global Usage Page.
        if (type == 1 && tag == 0) {
            usagePage = value;
            continue;
        }

        // Local Usage.
        if (type == 2 && tag == 0) {
            localUsage = value;
            continue;
        }

        if (type != 0) {
            continue;
        }

        // Collection (Application).
        if (
            tag == 0x0A &&
            value == 0x01 &&
            usagePage == USAGE_PAGE_GENERIC_DESKTOP
        ) {
            switch (localUsage) {
                case USAGE_DESKTOP_GAMEPAD:
                case USAGE_DESKTOP_JOYSTICK:
                    cache.hasGamepadApplication = true;
                    break;

                case USAGE_DESKTOP_KEYBOARD:
                    cache.hasKeyboardApplication = true;
                    break;

                case USAGE_DESKTOP_MOUSE:
                    cache.hasMouseApplication = true;
                    break;

                default:
                    break;
            }
        }

        // HID local items are scoped to the next Main item.
        localUsage = 0;
    }
}

static uint16_t reportIdForDescriptor(
    const uint8_t* descriptor,
    uint16_t descriptorLength,
    const uint8_t* report,
    uint16_t reportLength
) {
    if (
        descriptor == nullptr ||
        report == nullptr ||
        reportLength == 0
    ) {
        return HID_REPORT_ID_UNDEFINED;
    }

    return btstack_hid_report_id_declared(
        descriptor,
        descriptorLength
    )
        ? report[0]
        : HID_REPORT_ID_UNDEFINED;
}

static void populateHidCache(
    HidServiceCache& cache,
    uint8_t const* descriptor,
    uint16_t descriptorLength
) {
    cache = HidServiceCache {};
    cache.parsed = true;

    if (descriptor == nullptr || descriptorLength == 0) {
        return;
    }

    classifyTopLevelApplications(
        cache,
        descriptor,
        descriptorLength
    );

    btstack_hid_usage_iterator_t iterator {};
    btstack_hid_usage_iterator_init(
        &iterator,
        descriptor,
        descriptorLength,
        BT_HID_REPORT_TYPE_INPUT
    );

    bool hasAxis = false;
    bool hasButtons = false;
    bool hasKeyboardFields = false;
    bool hasAccelerator = false;
    bool hasBrake = false;
    bool hasXboxButton1 = false;
    bool hasXboxButton2 = false;
    bool hasXboxButton4 = false;
    bool hasXboxButton5 = false;
    bool hasXboxButton7 = false;
    bool hasXboxButton8 = false;
    bool hasXboxButton11 = false;
    bool hasXboxButton12 = false;
    bool hasXboxButton13 = false;
    bool hasXboxButton14 = false;
    bool hasXboxButton15 = false;

    while (btstack_hid_usage_iterator_has_more(&iterator)) {
        const int32_t logicalMin =
            iterator.global_logical_minimum;
        const int32_t logicalMax =
            iterator.global_logical_maximum;

        btstack_hid_usage_item_t item {};
        btstack_hid_usage_iterator_get_item(
            &iterator,
            &item
        );

        if (item.usage_page == 0 || item.usage == 0) {
            continue;
        }

        if (cache.fieldCount < MAX_FIELD_RANGES) {
            HidFieldRange& field =
                cache.fields[cache.fieldCount++];

            field.reportId = item.report_id;
            field.usagePage = item.usage_page;
            field.usage = item.usage;
            field.logicalMin = logicalMin;
            field.logicalMax = logicalMax;
        }

        HidReportMeta* meta =
            ensureReportMeta(cache, item.report_id);

        if (item.usage_page == USAGE_PAGE_KEYBOARD) {
            hasKeyboardFields = true;
            if (meta != nullptr) {
                meta->classFlags |= HID_REPORT_CLASS_KEYBOARD;
            }
        }

        if (item.usage_page == USAGE_PAGE_CONSUMER) {
            if (meta != nullptr) {
                meta->classFlags |= HID_REPORT_CLASS_CONSUMER;
            }
        }

        if (item.usage_page == USAGE_PAGE_BUTTON) {
            hasButtons = true;

            if (meta != nullptr) {
                meta->classFlags |= HID_REPORT_CLASS_BUTTONS;
            }

            switch (item.usage) {
                case 1:  hasXboxButton1 = true; break;
                case 2:  hasXboxButton2 = true; break;
                case 4:  hasXboxButton4 = true; break;
                case 5:  hasXboxButton5 = true; break;
                case 7:  hasXboxButton7 = true; break;
                case 8:  hasXboxButton8 = true; break;
                case 11: hasXboxButton11 = true; break;
                case 12: hasXboxButton12 = true; break;
                case 13: hasXboxButton13 = true; break;
                case 14: hasXboxButton14 = true; break;
                case 15: hasXboxButton15 = true; break;
                default: break;
            }
        }

        if (item.usage_page == USAGE_PAGE_SIMULATION) {
            if (item.usage == USAGE_SIM_ACCELERATOR) {
                hasAccelerator = true;
            } else if (item.usage == USAGE_SIM_BRAKE) {
                hasBrake = true;
            }
        }

        if (
            item.usage_page == USAGE_PAGE_GENERIC_DESKTOP &&
            (
                item.usage == USAGE_X ||
                item.usage == USAGE_Y ||
                item.usage == USAGE_Z ||
                item.usage == USAGE_RX ||
                item.usage == USAGE_RY ||
                item.usage == USAGE_RZ ||
                item.usage == USAGE_WHEEL ||
                item.usage == USAGE_HAT ||
                item.usage == USAGE_DPAD_UP ||
                item.usage == USAGE_DPAD_DOWN ||
                item.usage == USAGE_DPAD_RIGHT ||
                item.usage == USAGE_DPAD_LEFT
            )
        ) {
            hasAxis = true;

            if (
                meta != nullptr &&
                (
                    item.usage == USAGE_X ||
                    item.usage == USAGE_Y ||
                    item.usage == USAGE_WHEEL
                )
            ) {
                meta->classFlags |= HID_REPORT_CLASS_POINTER;
            }
        }
    }

    // Top-level Application usages are the primary discriminator. This avoids
    // treating a mouse (X/Y + buttons) as a gamepad.
    cache.looksLikeKeyboard =
        cache.hasKeyboardApplication ||
        hasKeyboardFields;

    cache.looksLikeMouse =
        cache.hasMouseApplication;

    cache.looksLikeGamepad =
        cache.hasGamepadApplication &&
        hasAxis &&
        hasButtons;

    // Legacy fallback for unusual gamepad descriptors that omit a recognizable
    // Game Pad/Joystick application usage. Never steal known keyboard/mouse HID.
    if (
        !cache.looksLikeGamepad &&
        !cache.looksLikeKeyboard &&
        !cache.looksLikeMouse &&
        hasAxis &&
        hasButtons
    ) {
        cache.looksLikeGamepad = true;
    }

    cache.xboxBleButtonLayout =
        cache.looksLikeGamepad &&
        hasAccelerator &&
        hasBrake &&
        hasXboxButton1 &&
        hasXboxButton2 &&
        hasXboxButton4 &&
        hasXboxButton5 &&
        hasXboxButton7 &&
        hasXboxButton8 &&
        hasXboxButton11 &&
        hasXboxButton12 &&
        hasXboxButton13 &&
        hasXboxButton14 &&
        hasXboxButton15;

    cache.profile =
        cache.xboxBleButtonLayout
            ? BluetoothGamepadProfile::XBOX_BLE
            : BluetoothGamepadProfile::GENERIC_HID;
}

static void handleGenericHidGamepadReport(
    HidServiceCache& cache,
    uint8_t const* descriptor,
    uint16_t descriptorLength,
    uint8_t const* report,
    uint16_t reportLength,
    UniversalTransport transport
) {
    if (
        report == nullptr ||
        reportLength == 0 ||
        descriptor == nullptr ||
        descriptorLength == 0
    ) {
        return;
    }

    if (!cache.parsed) {
        populateHidCache(
            cache,
            descriptor,
            descriptorLength
        );
    }

    if (!cache.looksLikeGamepad) {
        return;
    }

    uint16_t reportId = HID_REPORT_ID_UNDEFINED;

    if (
        btstack_hid_report_id_declared(
            descriptor,
            descriptorLength
        )
    ) {
        reportId = report[0];
    }

    btstack_hid_parser_t parser {};

    btstack_hid_parser_init(
        &parser,
        descriptor,
        descriptorLength,
        BT_HID_REPORT_TYPE_INPUT,
        report,
        reportLength
    );

    GamepadState next =
        bluetoothGamepadStateValid
            ? bluetoothGamepadState
            : GamepadState {};

    bool sawUsefulGamepadField = false;
    bool sawButtonPage = false;
    uint32_t reportButtons = 0;

    bool sawZ = false;
    bool sawRz = false;
    bool sawRx = false;
    bool sawRy = false;

    int32_t zValue = 0;
    int32_t zMin = 0;
    int32_t zMax = 0;

    int32_t rzValue = 0;
    int32_t rzMin = 0;
    int32_t rzMax = 0;

    bool sawAccelerator = false;
    bool sawBrake = false;

    int32_t acceleratorValue = 0;
    int32_t acceleratorMin = 0;
    int32_t acceleratorMax = 0;

    int32_t brakeValue = 0;
    int32_t brakeMin = 0;
    int32_t brakeMax = 0;

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

        int32_t logicalMin = 0;
        int32_t logicalMax = 0;

        findFieldRange(
            cache,
            reportId,
            usagePage,
            usage,
            logicalMin,
            logicalMax
        );

        if (usagePage == USAGE_PAGE_BUTTON) {
            sawButtonPage = true;
            sawUsefulGamepadField = true;

            if (value != 0) {
                reportButtons |=
                    buttonMaskForUsage(
                        usage,
                        cache.profile
                    );
            }

            continue;
        }

        if (usagePage == USAGE_PAGE_SIMULATION) {
            if (usage == USAGE_SIM_ACCELERATOR) {
                sawAccelerator = true;
                acceleratorValue = value;
                acceleratorMin = logicalMin;
                acceleratorMax = logicalMax;
                sawUsefulGamepadField = true;
            } else if (usage == USAGE_SIM_BRAKE) {
                sawBrake = true;
                brakeValue = value;
                brakeMin = logicalMin;
                brakeMax = logicalMax;
                sawUsefulGamepadField = true;
            }

            continue;
        }

        if (usagePage != USAGE_PAGE_GENERIC_DESKTOP) {
            continue;
        }

        switch (usage) {
            case USAGE_X:
                next.lx =
                    scaleAxis(
                        value,
                        logicalMin,
                        logicalMax
                    );
                sawUsefulGamepadField = true;
                break;

            case USAGE_Y:
                next.ly =
                    scaleAxis(
                        value,
                        logicalMin,
                        logicalMax
                    );
                sawUsefulGamepadField = true;
                break;

            case USAGE_RX:
                next.rx =
                    scaleAxis(
                        value,
                        logicalMin,
                        logicalMax
                    );
                sawRx = true;
                sawUsefulGamepadField = true;
                break;

            case USAGE_RY:
                next.ry =
                    scaleAxis(
                        value,
                        logicalMin,
                        logicalMax
                    );
                sawRy = true;
                sawUsefulGamepadField = true;
                break;

            case USAGE_Z:
                sawZ = true;
                zValue = value;
                zMin = logicalMin;
                zMax = logicalMax;
                sawUsefulGamepadField = true;
                break;

            case USAGE_RZ:
                sawRz = true;
                rzValue = value;
                rzMin = logicalMin;
                rzMax = logicalMax;
                sawUsefulGamepadField = true;
                break;

            case USAGE_HAT:
                next.dpad =
                    hatToDpad(
                        value,
                        logicalMin,
                        logicalMax
                    );
                next.dpadOriginal = next.dpad;
                sawUsefulGamepadField = true;
                break;

            case USAGE_DPAD_UP:
                if (value) next.dpad |= GAMEPAD_MASK_UP;
                else next.dpad &= ~GAMEPAD_MASK_UP;
                next.dpadOriginal = next.dpad;
                sawUsefulGamepadField = true;
                break;

            case USAGE_DPAD_DOWN:
                if (value) next.dpad |= GAMEPAD_MASK_DOWN;
                else next.dpad &= ~GAMEPAD_MASK_DOWN;
                next.dpadOriginal = next.dpad;
                sawUsefulGamepadField = true;
                break;

            case USAGE_DPAD_RIGHT:
                if (value) next.dpad |= GAMEPAD_MASK_RIGHT;
                else next.dpad &= ~GAMEPAD_MASK_RIGHT;
                next.dpadOriginal = next.dpad;
                sawUsefulGamepadField = true;
                break;

            case USAGE_DPAD_LEFT:
                if (value) next.dpad |= GAMEPAD_MASK_LEFT;
                else next.dpad &= ~GAMEPAD_MASK_LEFT;
                next.dpadOriginal = next.dpad;
                sawUsefulGamepadField = true;
                break;

            default:
                break;
        }
    }

    if (!sawUsefulGamepadField) {
        return;
    }

    if (sawButtonPage) {
        next.buttons = reportButtons;
    }

    if (
        !sawRx &&
        !sawRy &&
        sawZ &&
        sawRz &&
        (
            sawAccelerator ||
            sawBrake ||
            (zMin < 0 && rzMin < 0)
        )
    ) {
        next.rx =
            scaleAxis(
                zValue,
                zMin,
                zMax
            );

        next.ry =
            scaleAxis(
                rzValue,
                rzMin,
                rzMax
            );

        sawRx = true;
        sawRy = true;
    }

    if (sawBrake) {
        next.lt =
            scaleTrigger(
                brakeValue,
                brakeMin,
                brakeMax
            );
    } else if (sawZ && !sawRx && zMin >= 0) {
        next.lt =
            scaleTrigger(
                zValue,
                zMin,
                zMax
            );
    }

    if (sawAccelerator) {
        next.rt =
            scaleTrigger(
                acceleratorValue,
                acceleratorMin,
                acceleratorMax
            );
    } else if (sawRz && !sawRy && rzMin >= 0) {
        next.rt =
            scaleTrigger(
                rzValue,
                rzMin,
                rzMax
            );
    }

    if (next.lt != 0) {
        next.buttons |= GAMEPAD_MASK_L2;
    } else if (!sawButtonPage || (reportButtons & GAMEPAD_MASK_L2) == 0) {
        next.buttons &= ~GAMEPAD_MASK_L2;
    }

    if (next.rt != 0) {
        next.buttons |= GAMEPAD_MASK_R2;
    } else if (!sawButtonPage || (reportButtons & GAMEPAD_MASK_R2) == 0) {
        next.buttons &= ~GAMEPAD_MASK_R2;
    }

    ensureBluetoothSlotConnected(
        transport,
        cache.profile
    );

    if (!bluetoothSlotConnected) {
        return;
    }

    if (
        bluetoothGamepadStateValid &&
        std::memcmp(
            &bluetoothGamepadState,
            &next,
            sizeof(GamepadState)
        ) == 0
    ) {
        return;
    }

    bluetoothGamepadState = next;
    bluetoothGamepadStateValid = true;

    UINPUT.publish(
        UNIVERSAL_INPUT_SLOT_BLUETOOTH,
        bluetoothGamepadState
    );
}




static KeyboardReportContribution*
findOrCreateKeyboardContribution(
    HidServiceCache& cache,
    uint16_t reportId
) {
    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        KeyboardReportContribution& item =
            cache.keyboardReports[i];

        if (item.used && item.reportId == reportId) {
            return &item;
        }
    }

    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        KeyboardReportContribution& item =
            cache.keyboardReports[i];

        if (!item.used) {
            item = KeyboardReportContribution {};
            item.used = true;
            item.reportId = reportId;
            return &item;
        }
    }

    return nullptr;
}

static MouseReportContribution*
findOrCreateMouseContribution(
    HidServiceCache& cache,
    uint16_t reportId
) {
    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        MouseReportContribution& item =
            cache.mouseReports[i];

        if (item.used && item.reportId == reportId) {
            return &item;
        }
    }

    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        MouseReportContribution& item =
            cache.mouseReports[i];

        if (!item.used) {
            item = MouseReportContribution {};
            item.used = true;
            item.reportId = reportId;
            return &item;
        }
    }

    return nullptr;
}

static void appendConsumerUsage(
    UniversalKeyboardState& state,
    uint16_t usage
) {
    if (usage == 0) {
        return;
    }

    for (uint8_t i = 0; i < state.consumerUsageCount; i++) {
        if (state.consumerUsages[i] == usage) {
            return;
        }
    }

    if (
        state.consumerUsageCount >=
        UNIVERSAL_KEYBOARD_MAX_CONSUMER_USAGES
    ) {
        return;
    }

    state.consumerUsages[state.consumerUsageCount++] = usage;
}

static void mergeKeyboardCache(
    UniversalKeyboardState& out,
    HidServiceCache const& cache
) {
    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        KeyboardReportContribution const& contribution =
            cache.keyboardReports[i];

        if (!contribution.used) {
            continue;
        }

        for (
            uint8_t word = 0;
            word < UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS;
            word++
        ) {
            out.keys[word] |= contribution.state.keys[word];
        }

        out.modifiers |= contribution.state.modifiers;

        for (
            uint8_t c = 0;
            c < contribution.state.consumerUsageCount;
            c++
        ) {
            appendConsumerUsage(
                out,
                contribution.state.consumerUsages[c]
            );
        }
    }
}

static uint32_t mergeMouseButtons(
    HidServiceCache const& cache
) {
    uint32_t buttons = 0;

    for (uint8_t i = 0; i < MAX_HID_REPORT_CONTRIBUTIONS; i++) {
        if (cache.mouseReports[i].used) {
            buttons |= cache.mouseReports[i].buttons;
        }
    }

    return buttons;
}

static UniversalHumanInterfaceSource
humanInterfaceSourceForTransport(
    UniversalTransport transport
) {
    return transport == UniversalTransport::BLUETOOTH_LE
        ? UniversalHumanInterfaceSource::BLUETOOTH_LE_HID
        : UniversalHumanInterfaceSource::BLUETOOTH_CLASSIC_HID;
}

static void publishBluetoothKeyboardAggregate(
    UniversalTransport transport
) {
    UniversalKeyboardState aggregate {};

    if (transport == UniversalTransport::BLUETOOTH_LE) {
        for (uint8_t i = 0; i < hidsServiceCount; i++) {
            mergeKeyboardCache(
                aggregate,
                serviceCaches[i]
            );
        }
    } else {
        mergeKeyboardCache(
            aggregate,
            classicCache
        );
    }

    UHIDINPUT.connectKeyboard(
        UNIVERSAL_HID_SLOT_BLUETOOTH,
        humanInterfaceSourceForTransport(transport),
        transport,
        UniversalProtocol::HID_KEYBOARD,
        0,
        0,
        0,
        0
    );

    UHIDINPUT.publishKeyboard(
        UNIVERSAL_HID_SLOT_BLUETOOTH,
        aggregate
    );
}

static uint32_t aggregateBluetoothMouseButtons(
    UniversalTransport transport
) {
    uint32_t buttons = 0;

    if (transport == UniversalTransport::BLUETOOTH_LE) {
        for (uint8_t i = 0; i < hidsServiceCount; i++) {
            buttons |= mergeMouseButtons(
                serviceCaches[i]
            );
        }
    } else {
        buttons = mergeMouseButtons(
            classicCache
        );
    }

    return buttons;
}

static void handleGenericHidKeyboardReport(
    HidServiceCache& cache,
    uint8_t const* descriptor,
    uint16_t descriptorLength,
    uint8_t const* report,
    uint16_t reportLength,
    UniversalTransport transport
) {
    if (
        !cache.looksLikeKeyboard ||
        descriptor == nullptr ||
        descriptorLength == 0 ||
        report == nullptr ||
        reportLength == 0
    ) {
        return;
    }

    const uint16_t reportId =
        reportIdForDescriptor(
            descriptor,
            descriptorLength,
            report,
            reportLength
        );

    const HidReportMeta* meta =
        findReportMeta(cache, reportId);

    const bool ownsKeyboardPage =
        meta != nullptr &&
        (meta->classFlags & HID_REPORT_CLASS_KEYBOARD) != 0;

    const bool ownsConsumerPage =
        meta != nullptr &&
        (meta->classFlags & HID_REPORT_CLASS_CONSUMER) != 0;

    if (!ownsKeyboardPage && !ownsConsumerPage) {
        return;
    }

    KeyboardReportContribution* contribution =
        findOrCreateKeyboardContribution(
            cache,
            reportId
        );

    if (contribution == nullptr) {
        return;
    }

    if (ownsKeyboardPage) {
        for (
            uint8_t word = 0;
            word < UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS;
            word++
        ) {
            contribution->state.keys[word] = 0;
        }

        contribution->state.modifiers = 0;
    }

    if (ownsConsumerPage) {
        contribution->state.consumerUsageCount = 0;
        std::memset(
            contribution->state.consumerUsages,
            0,
            sizeof(contribution->state.consumerUsages)
        );
    }

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        descriptorLength,
        BT_HID_REPORT_TYPE_INPUT,
        report,
        reportLength
    );

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

        if (
            usagePage == USAGE_PAGE_KEYBOARD &&
            value != 0
        ) {
            if (usage >= 0xE0 && usage <= 0xE7) {
                contribution->state.modifiers |=
                    static_cast<uint8_t>(
                        1u << (usage - 0xE0)
                    );
            } else if (usage <= 0xFF) {
                contribution->state.setKeyDown(
                    static_cast<uint8_t>(usage),
                    true
                );
            }

            continue;
        }

        if (
            usagePage == USAGE_PAGE_CONSUMER &&
            value != 0
        ) {
            appendConsumerUsage(
                contribution->state,
                usage
            );
        }
    }

    publishBluetoothKeyboardAggregate(transport);
}

static void handleGenericHidMouseReport(
    HidServiceCache& cache,
    uint8_t const* descriptor,
    uint16_t descriptorLength,
    uint8_t const* report,
    uint16_t reportLength,
    UniversalTransport transport
) {
    if (
        !cache.looksLikeMouse ||
        descriptor == nullptr ||
        descriptorLength == 0 ||
        report == nullptr ||
        reportLength == 0
    ) {
        return;
    }

    const uint16_t reportId =
        reportIdForDescriptor(
            descriptor,
            descriptorLength,
            report,
            reportLength
        );

    MouseReportContribution* contribution =
        findOrCreateMouseContribution(
            cache,
            reportId
        );

    if (contribution == nullptr) {
        return;
    }

    const HidReportMeta* meta =
        findReportMeta(cache, reportId);

    if (
        meta != nullptr &&
        (meta->classFlags & HID_REPORT_CLASS_BUTTONS) != 0
    ) {
        contribution->buttons = 0;
    }

    UniversalMouseState next {};

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        descriptorLength,
        BT_HID_REPORT_TYPE_INPUT,
        report,
        reportLength
    );

    bool sawMouseField = false;

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

        if (
            usagePage == USAGE_PAGE_BUTTON &&
            usage >= 1 &&
            usage <= 32
        ) {
            const uint32_t mask =
                1u << (usage - 1);

            if (value != 0) {
                contribution->buttons |= mask;
            } else {
                contribution->buttons &= ~mask;
            }

            sawMouseField = true;
            continue;
        }

        if (usagePage == USAGE_PAGE_GENERIC_DESKTOP) {
            switch (usage) {
                case USAGE_X:
                    next.x = value;
                    sawMouseField = true;
                    break;

                case USAGE_Y:
                    next.y = value;
                    sawMouseField = true;
                    break;

                case USAGE_WHEEL:
                    next.wheel = value;
                    sawMouseField = true;
                    break;

                default:
                    break;
            }

            continue;
        }

        if (
            usagePage == USAGE_PAGE_CONSUMER &&
            usage == USAGE_CONSUMER_AC_PAN
        ) {
            next.horizontalWheel = value;
            sawMouseField = true;
        }
    }

    if (!sawMouseField) {
        return;
    }

    next.buttons =
        aggregateBluetoothMouseButtons(transport);

    UHIDINPUT.connectMouse(
        UNIVERSAL_HID_SLOT_BLUETOOTH,
        humanInterfaceSourceForTransport(transport),
        transport,
        UniversalProtocol::HID_MOUSE,
        0,
        0,
        0,
        0
    );

    UHIDINPUT.publishMouse(
        UNIVERSAL_HID_SLOT_BLUETOOTH,
        next
    );
}

static void routeBluetoothHidReport(
    HidServiceCache& cache,
    uint8_t const* descriptor,
    uint16_t descriptorLength,
    uint8_t const* report,
    uint16_t reportLength,
    UniversalTransport transport
) {
    if (cache.looksLikeGamepad) {
        handleGenericHidGamepadReport(
            cache,
            descriptor,
            descriptorLength,
            report,
            reportLength,
            transport
        );
    }

    if (cache.looksLikeKeyboard) {
        handleGenericHidKeyboardReport(
            cache,
            descriptor,
            descriptorLength,
            report,
            reportLength,
            transport
        );
    }

    if (cache.looksLikeMouse) {
        handleGenericHidMouseReport(
            cache,
            descriptor,
            descriptorLength,
            report,
            reportLength,
            transport
        );
    }
}

static uint8_t scaleMotor255To100(uint8_t value) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(value) * 100U) / 255U
    );
}

static void serviceXboxBleFeedback(
    UniversalFeedbackSlotSnapshot const& feedback
) {
    if (
        hidsCid == 0 ||
        bleState != BleHostState::READY ||
        activeBluetoothProfile != BluetoothGamepadProfile::XBOX_BLE
    ) {
        return;
    }

    uint8_t report[8] {};

    const uint8_t strong =
        scaleMotor255To100(feedback.leftMotor);
    const uint8_t weak =
        scaleMotor255To100(feedback.rightMotor);
    const uint8_t leftTrigger =
        scaleMotor255To100(feedback.leftTrigger);
    const uint8_t rightTrigger =
        scaleMotor255To100(feedback.rightTrigger);

    uint8_t actuatorMask = 0;

    if (weak != 0) actuatorMask |= 0x01;
    if (strong != 0) actuatorMask |= 0x02;
    if (rightTrigger != 0) actuatorMask |= 0x04;
    if (leftTrigger != 0) actuatorMask |= 0x08;

    // Xbox BLE Output Report 0x03.
    // Payload order follows the controller HID descriptor:
    // mask, LT, RT, strong, weak, duration, delay, loop count.
    report[0] = actuatorMask == 0 ? 0x0F : actuatorMask;
    report[1] = leftTrigger;
    report[2] = rightTrigger;
    report[3] = strong;
    report[4] = weak;

    if (actuatorMask != 0) {
        report[5] = 0xFF;
        report[6] = 0x00;
        report[7] = 25;
    }

    const uint8_t status =
        hids_client_send_write_report(
            hidsCid,
            0x03,
            BT_HID_REPORT_TYPE_OUTPUT,
            report,
            sizeof(report)
        );

    if (status == ERROR_CODE_SUCCESS) {
        lastBluetoothFeedbackGeneration =
            feedback.generation;
    }
}

static uint32_t crc32Le(
    uint32_t seed,
    const uint8_t* data,
    size_t length
) {
    uint32_t crc = seed;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            crc =
                (crc >> 1) ^
                ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }

    return crc;
}

static void serviceDs4ClassicFeedback(
    UniversalFeedbackSlotSnapshot const& feedback
) {
    if (
        classicHidCid == 0 ||
        !classicDescriptorAvailable ||
        activeBluetoothProfile !=
            BluetoothGamepadProfile::SONY_DS4_CLASSIC
    ) {
        return;
    }

    // BTstack adds the HIDP DATA/OUTPUT byte and report ID. Build the same
    // bytes here only for CRC calculation, then send the payload after ID.
    uint8_t packet[79] {};
    packet[0] = 0xA2; // HIDP DATA | OUTPUT
    packet[1] = 0x11; // DS4 Bluetooth output report ID

    uint8_t* payload = &packet[2];

    payload[0] = 0xC4;
    payload[2] = 0x01; // enable rumble update
    payload[5] = feedback.rightMotor; // weak / small motor
    payload[6] = feedback.leftMotor;  // strong / large motor

    const uint32_t crc =
        ~crc32Le(
            0xFFFFFFFFU,
            packet,
            sizeof(packet) - 4
        );

    const size_t crcOffset = sizeof(packet) - 4;
    packet[crcOffset + 0] = static_cast<uint8_t>(crc >> 0);
    packet[crcOffset + 1] = static_cast<uint8_t>(crc >> 8);
    packet[crcOffset + 2] = static_cast<uint8_t>(crc >> 16);
    packet[crcOffset + 3] = static_cast<uint8_t>(crc >> 24);

    const uint8_t status =
        hid_host_send_report(
            classicHidCid,
            0x11,
            payload,
            sizeof(packet) - 2
        );

    if (status == ERROR_CODE_SUCCESS) {
        lastBluetoothFeedbackGeneration =
            feedback.generation;
    }
}

static void serviceDualSenseClassicFeedback(
    UniversalFeedbackSlotSnapshot const& feedback
) {
    if (
        classicHidCid == 0 ||
        !classicDescriptorAvailable ||
        activeBluetoothProfile !=
            BluetoothGamepadProfile::SONY_DUALSENSE_CLASSIC
    ) {
        return;
    }

    // DualSense Bluetooth output report:
    // report ID 0x31, seq/tag, 47-byte common block, 24 reserved, CRC32.
    uint8_t report[78] {};

    report[0] = 0x31;
    report[1] =
        static_cast<uint8_t>(
            (dualsenseOutputSequence & 0x0F) << 4
        );
    report[2] = 0x10;

    dualsenseOutputSequence =
        static_cast<uint8_t>(
            (dualsenseOutputSequence + 1) & 0x0F
        );

    uint8_t* common = &report[3];

    // Compatible vibration + select classic motor haptics.
    common[0] = 0x03;
    common[2] = feedback.rightMotor; // weak / small motor
    common[3] = feedback.leftMotor;  // strong / large motor

    const uint8_t seed = 0xA2;
    uint32_t crc =
        crc32Le(
            0xFFFFFFFFU,
            &seed,
            1
        );

    crc =
        ~crc32Le(
            crc,
            report,
            sizeof(report) - 4
        );

    const size_t crcOffset = sizeof(report) - 4;
    report[crcOffset + 0] = static_cast<uint8_t>(crc >> 0);
    report[crcOffset + 1] = static_cast<uint8_t>(crc >> 8);
    report[crcOffset + 2] = static_cast<uint8_t>(crc >> 16);
    report[crcOffset + 3] = static_cast<uint8_t>(crc >> 24);

    const uint8_t status =
        hid_host_send_report(
            classicHidCid,
            0x31,
            &report[1],
            sizeof(report) - 1
        );

    if (status == ERROR_CODE_SUCCESS) {
        lastBluetoothFeedbackGeneration =
            feedback.generation;
    }
}

static void encodeSwitchRumbleMotor(
    uint8_t magnitude,
    uint8_t out[4]
) {
    if (magnitude == 0) {
        out[0] = 0x00;
        out[1] = 0x01;
        out[2] = 0x40;
        out[3] = 0x40;
        return;
    }

    // Fixed 160/80 Hz carrier pair with stepped amplitudes from the
    // documented Nintendo HD-rumble encoding table.
    out[0] = 0x80;

    if (magnitude < 64) {
        out[1] = 0x24;
        out[2] = 0x20;
        out[3] = 0x49;
    } else if (magnitude < 128) {
        out[1] = 0x4A;
        out[2] = 0xA0;
        out[3] = 0x52;
    } else if (magnitude < 192) {
        out[1] = 0x88;
        out[2] = 0x20;
        out[3] = 0x62;
    } else {
        out[1] = 0xC8;
        out[2] = 0x20;
        out[3] = 0x72;
    }
}

static void serviceSwitchClassicFeedback(
    UniversalFeedbackSlotSnapshot const& feedback
) {
    if (
        classicHidCid == 0 ||
        !classicDescriptorAvailable ||
        activeBluetoothProfile !=
            BluetoothGamepadProfile::NINTENDO_SWITCH_CLASSIC
    ) {
        return;
    }

    uint8_t payload[9] {};

    payload[0] = switchOutputPacketNumber;
    switchOutputPacketNumber =
        static_cast<uint8_t>(
            (switchOutputPacketNumber + 1) & 0x0F
        );

    encodeSwitchRumbleMotor(
        feedback.leftMotor,
        &payload[1]
    );

    encodeSwitchRumbleMotor(
        feedback.rightMotor,
        &payload[5]
    );

    const uint8_t status =
        hid_host_send_report(
            classicHidCid,
            0x10,
            payload,
            sizeof(payload)
        );

    if (status == ERROR_CODE_SUCCESS) {
        lastBluetoothFeedbackGeneration =
            feedback.generation;
    }
}

static void serviceBluetoothFeedback() {
    const uint64_t nowUs = time_us_64();

    if (
        nowUs - lastBluetoothFeedbackAttemptUs <
        50000
    ) {
        return;
    }

    UniversalFeedbackSlotSnapshot feedback {};

    if (
        !UFEEDBACK.snapshot(
            UNIVERSAL_INPUT_SLOT_BLUETOOTH,
            feedback
        ) ||
        !feedback.valid
    ) {
        return;
    }

    if (
        feedback.generation ==
        lastBluetoothFeedbackGeneration
    ) {
        return;
    }

    lastBluetoothFeedbackAttemptUs = nowUs;

    switch (activeBluetoothProfile) {
        case BluetoothGamepadProfile::XBOX_BLE:
            serviceXboxBleFeedback(feedback);
            break;

        case BluetoothGamepadProfile::SONY_DS4_CLASSIC:
            serviceDs4ClassicFeedback(feedback);
            break;

        case BluetoothGamepadProfile::SONY_DUALSENSE_CLASSIC:
            serviceDualSenseClassicFeedback(feedback);
            break;

        case BluetoothGamepadProfile::NINTENDO_SWITCH_CLASSIC:
            serviceSwitchClassicFeedback(feedback);
            break;

        case BluetoothGamepadProfile::GENERIC_HID:
        default:
            // Generic HID has no universal rumble report format. Input stays
            // fully functional; feedback is enabled only by a verified profile.
            break;
    }
}

static void handleBleHidReport(
    uint8_t serviceIndex,
    uint8_t const* report,
    uint16_t reportLength
) {
    if (
        report == nullptr ||
        reportLength == 0 ||
        serviceIndex >= hidsServiceCount
    ) {
        return;
    }

    HidServiceCache* cache =
        ensureServiceCache(serviceIndex);

    if (cache == nullptr) {
        return;
    }

    const uint8_t* descriptor =
        hids_client_descriptor_storage_get_descriptor_data(
            hidsCid,
            serviceIndex
        );

    const uint16_t descriptorLength =
        hids_client_descriptor_storage_get_descriptor_len(
            hidsCid,
            serviceIndex
        );

    routeBluetoothHidReport(
        *cache,
        descriptor,
        descriptorLength,
        report,
        reportLength,
        UniversalTransport::BLUETOOTH_LE
    );
}

static BluetoothGamepadProfile detectClassicProfileFromReport(
    const uint8_t* report,
    uint16_t reportLength
) {
    if (report == nullptr || reportLength == 0) {
        return BluetoothGamepadProfile::GENERIC_HID;
    }

    if (report[0] == 0x31 && reportLength >= 70) {
        return BluetoothGamepadProfile::SONY_DUALSENSE_CLASSIC;
    }

    if (
        (report[0] == 0x11 && reportLength >= 70) ||
        (report[0] == 0x01 && reportLength == 10)
    ) {
        return BluetoothGamepadProfile::SONY_DS4_CLASSIC;
    }

    if (
        report[0] == 0x3F ||
        report[0] == 0x30 ||
        report[0] == 0x21
    ) {
        return BluetoothGamepadProfile::NINTENDO_SWITCH_CLASSIC;
    }

    return BluetoothGamepadProfile::GENERIC_HID;
}

static void handleClassicHidReport(
    uint8_t const* report,
    uint16_t reportLength
) {
    if (
        report == nullptr ||
        reportLength == 0 ||
        !classicDescriptorAvailable ||
        classicHidCid == 0
    ) {
        return;
    }

    // Bluetooth Classic HID interrupt transactions include the DATA
    // transaction header 0xA1 before the HID report payload.
    if (report[0] == 0xA1) {
        report++;
        reportLength--;
    }

    if (reportLength == 0) {
        return;
    }

    if (
        classicCache.profile ==
            BluetoothGamepadProfile::GENERIC_HID
    ) {
        const BluetoothGamepadProfile runtimeProfile =
            detectClassicProfileFromReport(
                report,
                reportLength
            );

        if (
            runtimeProfile !=
                BluetoothGamepadProfile::GENERIC_HID
        ) {
            classicCache.profile = runtimeProfile;
            classicRemote.profile =
                static_cast<uint8_t>(runtimeProfile);
            classicRemotePersisted = false;
            saveStoredClassicRemote();
        }
    }

    const uint8_t* descriptor =
        hid_descriptor_storage_get_descriptor_data(
            classicHidCid
        );

    const uint16_t descriptorLength =
        hid_descriptor_storage_get_descriptor_len(
            classicHidCid
        );

    routeBluetoothHidReport(
        classicCache,
        descriptor,
        descriptorLength,
        report,
        reportLength,
        UniversalTransport::BLUETOOTH_CLASSIC
    );
}

static void handleGattClientEvent(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
    uint16_t size
) {
    (void)packetType;
    (void)channel;
    (void)size;

    if (
        packet == nullptr ||
        hci_event_packet_get_type(packet) !=
            HCI_EVENT_GATTSERVICE_META
    ) {
        return;
    }

    const uint8_t subevent =
        hci_event_gattservice_meta_get_subevent_code(
            packet
        );

    switch (subevent) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED:
        {
            const uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(
                    packet
                );

            if (status != ERROR_CODE_SUCCESS) {
                if (connectionHandle != HCI_CON_HANDLE_INVALID) {
                    gap_disconnect(connectionHandle);
                }
                return;
            }

            hidsServiceCount =
                gattservice_subevent_hid_service_connected_get_num_instances(
                    packet
                );

            if (hidsServiceCount > MAX_HID_SERVICES) {
                hidsServiceCount = MAX_HID_SERVICES;
            }

            resetServiceCaches();

            for (
                uint8_t service = 0;
                service < hidsServiceCount;
                service++
            ) {
                ensureServiceCache(service);
            }

            bleState = BleHostState::READY;
            saveStoredRemote();
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT:
            handleBleHidReport(
                gattservice_subevent_hid_report_get_service_index(
                    packet
                ),
                gattservice_subevent_hid_report_get_report(
                    packet
                ),
                gattservice_subevent_hid_report_get_report_len(
                    packet
                )
            );
            break;

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
            resetBluetoothGamepadState();
            resetBluetoothHumanInterfaceState();
            resetServiceCaches();
            hidsCid = 0;
            hidsServiceCount = 0;
            break;

        default:
            break;
    }
}

static void smPacketHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
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

        case SM_EVENT_PAIRING_COMPLETE:
            if (
                sm_event_pairing_complete_get_status(
                    packet
                ) ==
                ERROR_CODE_SUCCESS
            ) {
                connectHids();
            } else if (
                connectionHandle != HCI_CON_HANDLE_INVALID
            ) {
                gap_disconnect(connectionHandle);
            }
            break;

        case SM_EVENT_REENCRYPTION_COMPLETE:
            if (
                sm_event_reencryption_complete_get_status(
                    packet
                ) ==
                ERROR_CODE_SUCCESS
            ) {
                connectHids();
            } else if (
                connectionHandle != HCI_CON_HANDLE_INVALID
            ) {
                gap_disconnect(connectionHandle);
            }
            break;

        default:
            break;
    }
}

static void btPacketHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
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

    const uint8_t event =
        hci_event_packet_get_type(packet);

    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (
                btstack_event_state_get_state(packet) !=
                HCI_STATE_WORKING
            ) {
                return;
            }

            if (remoteKnown) {
                connectStoredRemote();
            } else if (classicRemoteKnown) {
                connectStoredClassic();
            } else {
                startBleScan();
            }
            break;

        case GAP_EVENT_INQUIRY_RESULT:
        {
            if (discoveryMode != DiscoveryMode::CLASSIC) {
                return;
            }

            const uint32_t classOfDevice =
                gap_event_inquiry_result_get_class_of_device(
                    packet
                );

            // Bluetooth Major Device Class 0x05 = Peripheral. HID gamepads,
            // keyboards and mice live here; the descriptor decides the final
            // class after the L2CAP HID connection is established.
            if ((classOfDevice & 0x1F00u) != 0x0500u) {
                return;
            }

            bd_addr_t candidate {};
            gap_event_inquiry_result_get_bd_addr(
                packet,
                candidate
            );

            gap_inquiry_stop();
            discoveryMode = DiscoveryMode::NONE;

            std::memcpy(
                classicRemote.address,
                candidate,
                sizeof(classicRemote.address)
            );

            classicRemote.profile =
                static_cast<uint8_t>(
                    classifyClassicInquiry(packet)
                );

            classicRemoteKnown = true;
            classicRemotePersisted = false;
            classicConnecting = true;

            const uint8_t status = hid_host_connect(
                classicRemote.address,
                HID_PROTOCOL_MODE_REPORT,
                &classicHidCid
            );

            if (status != ERROR_CODE_SUCCESS) {
                classicConnecting = false;
                classicHidCid = 0;
                startBleScan();
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (
                discoveryMode == DiscoveryMode::CLASSIC &&
                !classicConnecting &&
                classicHidCid == 0 &&
                !bluetoothSlotConnected
            ) {
                startBleScan();
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT:
            if (
                bleState != BleHostState::SCANNING ||
                !advertisementLooksLikeHid(packet)
            ) {
                return;
            }

            stopDiscoveryTimer();
            gap_stop_scan();
            discoveryMode = DiscoveryMode::NONE;

            gap_event_advertising_report_get_address(
                packet,
                remoteDevice.address
            );

            remoteDevice.addressType =
                gap_event_advertising_report_get_address_type(
                    packet
                );

            remoteKnown = true;
            remotePersisted = false;
            connectStoredRemote();
            break;

        case HCI_EVENT_META_GAP:
            if (
                hci_event_gap_meta_get_subevent_code(packet) !=
                    GAP_SUBEVENT_LE_CONNECTION_COMPLETE
            ) {
                return;
            }

            if (
                gap_subevent_le_connection_complete_get_status(
                    packet
                ) !=
                ERROR_CODE_SUCCESS
            ) {
                connectionHandle =
                    HCI_CON_HANDLE_INVALID;

                startBleScan();
                return;
            }

            stopDiscoveryTimer();

            connectionHandle =
                gap_subevent_le_connection_complete_get_connection_handle(
                    packet
                );

            discoveryMode = DiscoveryMode::NONE;
            bleState = BleHostState::PAIRING;

            sm_request_pairing(
                connectionHandle
            );
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (connectionHandle == HCI_CON_HANDLE_INVALID) {
                return;
            }

            if (
                hci_event_disconnection_complete_get_connection_handle(
                    packet
                ) !=
                    connectionHandle
            ) {
                return;
            }

            connectionHandle =
                HCI_CON_HANDLE_INVALID;

            hidsCid = 0;
            hidsServiceCount = 0;

            resetBluetoothGamepadState();
            resetBluetoothHumanInterfaceState();
            resetServiceCaches();

            // Continuous discovery: after any BLE disconnect, return to the
            // BLE -> Classic -> BLE discovery cycle instead of getting stuck
            // retrying one stale device forever.
            startBleScan();
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
        {
            bd_addr_t address {};
            hci_event_pin_code_request_get_bd_addr(
                packet,
                address
            );
            gap_pin_code_negative(address);
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
        {
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

        case HCI_EVENT_HID_META:
        {
            const uint8_t subevent =
                hci_event_hid_meta_get_subevent_code(
                    packet
                );

            switch (subevent) {
                case HID_SUBEVENT_INCOMING_CONNECTION:
                    hid_host_accept_connection(
                        hid_subevent_incoming_connection_get_hid_cid(
                            packet
                        ),
                        HID_PROTOCOL_MODE_REPORT
                    );
                    break;

                case HID_SUBEVENT_CONNECTION_OPENED:
                {
                    const uint8_t status =
                        hid_subevent_connection_opened_get_status(
                            packet
                        );

                    if (status != ERROR_CODE_SUCCESS) {
                        classicConnecting = false;
                        classicHidCid = 0;
                        classicDescriptorAvailable = false;
                        startBleScan();
                        break;
                    }

                    classicConnecting = false;
                    classicHidCid =
                        hid_subevent_connection_opened_get_hid_cid(
                            packet
                        );

                    hid_subevent_connection_opened_get_bd_addr(
                        packet,
                        classicRemote.address
                    );

                    classicRemoteKnown = true;
                    classicRemotePersisted = false;
                    classicDescriptorAvailable = false;
                    classicCache = HidServiceCache {};
                    break;
                }

                case HID_SUBEVENT_DESCRIPTOR_AVAILABLE:
                {
                    if (
                        hid_subevent_descriptor_available_get_status(
                            packet
                        ) !=
                            ERROR_CODE_SUCCESS
                    ) {
                        if (classicHidCid != 0) {
                            hid_host_disconnect(
                                classicHidCid
                            );
                        }
                        break;
                    }

                    const uint8_t* descriptor =
                        hid_descriptor_storage_get_descriptor_data(
                            classicHidCid
                        );

                    const uint16_t descriptorLength =
                        hid_descriptor_storage_get_descriptor_len(
                            classicHidCid
                        );

                    const BluetoothGamepadProfile detectedProfile =
                        static_cast<BluetoothGamepadProfile>(
                            classicRemote.profile
                        );

                    populateHidCache(
                        classicCache,
                        descriptor,
                        descriptorLength
                    );

                    if (
                        detectedProfile !=
                            BluetoothGamepadProfile::GENERIC_HID
                    ) {
                        classicCache.profile = detectedProfile;
                    }

                    if (
                        !classicCache.looksLikeGamepad &&
                        !classicCache.looksLikeKeyboard &&
                        !classicCache.looksLikeMouse
                    ) {
                        hid_host_disconnect(
                            classicHidCid
                        );
                        break;
                    }

                    classicDescriptorAvailable = true;
                    saveStoredClassicRemote();
                    break;
                }

                case HID_SUBEVENT_REPORT:
                    if (classicDescriptorAvailable) {
                        handleClassicHidReport(
                            hid_subevent_report_get_report(
                                packet
                            ),
                            hid_subevent_report_get_report_len(
                                packet
                            )
                        );
                    }
                    break;

                case HID_SUBEVENT_CONNECTION_CLOSED:
                    classicConnecting = false;
                    classicHidCid = 0;
                    classicDescriptorAvailable = false;
                    classicCache = HidServiceCache {};
                    resetBluetoothGamepadState();
                    resetBluetoothHumanInterfaceState();

                    if (
                        connectionHandle ==
                            HCI_CON_HANDLE_INVALID
                    ) {
                        startBleScan();
                    }
                    break;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

} // namespace

#endif

bool UniversalBluetoothHostAddon::available() {
#if OAG_BLUETOOTH_HOST_ENABLED && OAG_BLUETOOTH_PLATFORM_SUPPORTED
    return true;
#else
    return false;
#endif
}

void UniversalBluetoothHostAddon::setup() {
#if OAG_BLUETOOTH_HOST_ENABLED && OAG_BLUETOOTH_PLATFORM_SUPPORTED
    // Defer CYW43/BTstack until the main run loop, after USB Host is started.
    initialized = false;
    bleState = BleHostState::WAITING_HCI;
#endif
}

void UniversalBluetoothHostAddon::preprocess() {
#if OAG_BLUETOOTH_HOST_ENABLED && OAG_BLUETOOTH_PLATFORM_SUPPORTED
    static uint64_t initNotBeforeUs = 0;

    if (!initialized) {
        const uint64_t nowUs = time_us_64();

        // Match the hardware-proven legacy firmware:
        // USB Host first, then a 100 ms settling period, then CYW43/BTstack.
        if (initNotBeforeUs == 0) {
            initNotBeforeUs = nowUs + 100000;
            return;
        }

        if (nowUs < initNotBeforeUs) {
            return;
        }

        if (cyw43_arch_init() != PICO_OK) {
            // Retry slowly instead of hammering the CYW43 init path.
            initNotBeforeUs = nowUs + 1000000;
            return;
        }

        initNotBeforeUs = 0;

        resetBluetoothGamepadState();
        resetBluetoothHumanInterfaceState();
        resetServiceCaches();

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
            classicDescriptorStorage,
            sizeof(classicDescriptorStorage)
        );

        hid_host_register_packet_handler(
            btPacketHandler
        );

        hids_client_init(
            leDescriptorStorage,
            sizeof(leDescriptorStorage)
        );

        gap_set_local_name(
            OAG_BLUETOOTH_DEVICE_NAME
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

        hciEventCallback.callback =
            &btPacketHandler;

        hci_add_event_handler(
            &hciEventCallback
        );

        smEventCallback.callback =
            &smPacketHandler;

        sm_add_event_handler(
            &smEventCallback
        );

        gap_connectable_control(0);
        gap_discoverable_control(0);

        loadStoredClassicRemote();
        const bool haveBleRemote = loadStoredRemote();

        if (!haveBleRemote && !classicRemoteKnown) {
            gap_delete_all_link_keys();
            clearBleBondDatabase();
        }

        bleState = BleHostState::WAITING_HCI;
        initialized = true;

        hci_power_control(
            HCI_POWER_ON
        );

        serviceBluetoothDiagnosticLed();
        return;
    }

    // pico_btstack_cyw43 is serviced by the SDK async-context run loop.
    serviceBluetoothFeedback();
    serviceBluetoothDiagnosticLed();
#endif
}
