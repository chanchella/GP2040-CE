/*
 * OAG BLE phone gamepad compatibility layer.
 *
 * ATT fragments are ported from Arduino-Pico 6.1.1
 * PicoBluetoothBLEHID (LGPL-2.1-or-later), whose JoystickBLE path was
 * hardware-proven on this Pico 2 W + phone. OAG removes its stack ownership:
 * BluetoothHostV2 remains the only CYW43/BTstack/HCI owner.
 */

#include "oag/firmware/ble_phone_gamepad_output.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "btstack.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "oag/input/gamepad_state.h"

namespace {

oag::firmware::BlePhoneGamepadOutput* gPhoneGamepadOutput = nullptr;

void phoneHidsThunk(
    std::uint8_t packetType,
    std::uint16_t channel,
    std::uint8_t* packet,
    std::uint16_t size
) {
    if (gPhoneGamepadOutput != nullptr) {
        gPhoneGamepadOutput->handleHidsPacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}

constexpr std::uint8_t kHidDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32,
    0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F,
    0x95, 0x06, 0x75, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39,
    0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3B, 0x01,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x20,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x20, 0x75, 0x01, 0x81, 0x02,
    0xC0
};

constexpr char kPhoneName[] = "OAG UI5K BT BRIDGE";

constexpr std::uint8_t kAdvertisingData[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x0E, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'O','A','G',' ','B','L','E',' ','P','R','O','B','E',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF
    ),
    static_cast<std::uint8_t>(
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8
    ),
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

constexpr std::uint8_t kAttHead[] = {
        // ATT DB Version
        1,

        // 0x0001 PRIMARY_SERVICE-GAP_SERVICE
        0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,
        // 0x0002 CHARACTERISTIC-GAP_DEVICE_NAME - READ
        0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a,
};

constexpr std::uint8_t kAttBatteryAndHidHead[] = {
        // #import <battery_service.gatt> -- BEGIN
        // Specification Type org.bluetooth.service.battery_service
        // https://www.bluetooth.com/api/gatt/xmlfile?xmlFileName=org.bluetooth.service.battery_service.xml
        // Battery Service 180F
        // 0x0004 PRIMARY_SERVICE-ORG_BLUETOOTH_SERVICE_BATTERY_SERVICE
        0x0a, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0x0f, 0x18,
        // 0x0005 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL - DYNAMIC | READ | NOTIFY
        0x0d, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x12, 0x06, 0x00, 0x19, 0x2a,
        // 0x0006 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL - DYNAMIC | READ | NOTIFY
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x06, 0x00, 0x19, 0x2a,
        // 0x0007 CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ANYBODY
        0x0a, 0x00, 0x0e, 0x01, 0x07, 0x00, 0x02, 0x29, 0x00, 0x00,
        // #import <battery_service.gatt> -- END
        // add Device ID Service


        // #import <device_information_service.gatt> -- BEGIN
        // Specification Type org.bluetooth.service.device_information
        // https://www.bluetooth.com/api/gatt/xmlfile?xmlFileName=org.bluetooth.service.device_information.xml
        // Device Information 180A
        // 0x0008 PRIMARY_SERVICE-ORG_BLUETOOTH_SERVICE_DEVICE_INFORMATION
        0x0a, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x28, 0x0a, 0x18,
        // 0x0009 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_MANUFACTURER_NAME_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x09, 0x00, 0x03, 0x28, 0x02, 0x0a, 0x00, 0x29, 0x2a,
        // 0x000a VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_MANUFACTURER_NAME_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x0a, 0x00, 0x29, 0x2a,
        // 0x000b CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_MODEL_NUMBER_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x03, 0x28, 0x02, 0x0c, 0x00, 0x24, 0x2a,
        // 0x000c VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_MODEL_NUMBER_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x0c, 0x00, 0x24, 0x2a,
        // 0x000d CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SERIAL_NUMBER_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x0d, 0x00, 0x03, 0x28, 0x02, 0x0e, 0x00, 0x25, 0x2a,
        // 0x000e VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SERIAL_NUMBER_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x0e, 0x00, 0x25, 0x2a,
        // 0x000f CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HARDWARE_REVISION_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x03, 0x28, 0x02, 0x10, 0x00, 0x27, 0x2a,
        // 0x0010 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HARDWARE_REVISION_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x10, 0x00, 0x27, 0x2a,
        // 0x0011 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_FIRMWARE_REVISION_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x11, 0x00, 0x03, 0x28, 0x02, 0x12, 0x00, 0x26, 0x2a,
        // 0x0012 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_FIRMWARE_REVISION_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x12, 0x00, 0x26, 0x2a,
        // 0x0013 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SOFTWARE_REVISION_STRING - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x13, 0x00, 0x03, 0x28, 0x02, 0x14, 0x00, 0x28, 0x2a,
        // 0x0014 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SOFTWARE_REVISION_STRING - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x14, 0x00, 0x28, 0x2a,
        // 0x0015 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SYSTEM_ID - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x15, 0x00, 0x03, 0x28, 0x02, 0x16, 0x00, 0x23, 0x2a,
        // 0x0016 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_SYSTEM_ID - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x16, 0x00, 0x23, 0x2a,
        // 0x0017 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_IEEE_11073_20601_REGULATORY_CERTIFICATION_DATA_LIST - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x17, 0x00, 0x03, 0x28, 0x02, 0x18, 0x00, 0x2a, 0x2a,
        // 0x0018 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_IEEE_11073_20601_REGULATORY_CERTIFICATION_DATA_LIST - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x18, 0x00, 0x2a, 0x2a,
        // 0x0019 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_PNP_ID - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x19, 0x00, 0x03, 0x28, 0x02, 0x1a, 0x00, 0x50, 0x2a,
        // 0x001a VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_PNP_ID - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x1a, 0x00, 0x50, 0x2a,
        // #import <device_information_service.gatt> -- END

        // Specification Type org.bluetooth.service.human_interface_device
        // https://www.bluetooth.com/api/gatt/xmlfile?xmlFileName=org.bluetooth.service.human_interface_device.xml
        // Human Interface Device 1812
        // 0x001b PRIMARY_SERVICE-ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE
        0x0a, 0x00, 0x02, 0x00, 0x1b, 0x00, 0x00, 0x28, 0x12, 0x18,
        // 0x001c CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_PROTOCOL_MODE - DYNAMIC | READ | WRITE_WITHOUT_RESPONSE
        0x0d, 0x00, 0x02, 0x00, 0x1c, 0x00, 0x03, 0x28, 0x06, 0x1d, 0x00, 0x4e, 0x2a,
        // 0x001d VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_PROTOCOL_MODE - DYNAMIC | READ | WRITE_WITHOUT_RESPONSE
        // READ_ANYBODY, WRITE_ANYBODY
        0x08, 0x00, 0x06, 0x01, 0x1d, 0x00, 0x4e, 0x2a,
};

constexpr std::uint8_t kAttKeyboardReports[] = {
        // 0x001e CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        0x0d, 0x00, 0x02, 0x00, 0x1e, 0x00, 0x03, 0x28, 0x1a, 0x1f, 0x00, 0x4d, 0x2a,
        // 0x001f VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        // READ_ENCRYPTED, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x08, 0x00, 0x0b, 0xf5, 0x1f, 0x00, 0x4d, 0x2a,
        // 0x0020 CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x0a, 0x00, 0x0f, 0xf1, 0x20, 0x00, 0x02, 0x29, 0x00, 0x00,
        // fixed report id = 1, type = Input (1); keycodes
        // 0x0021 REPORT_REFERENCE-READ-1-1
        0x0a, 0x00, 0x02, 0x00, 0x21, 0x00, 0x08, 0x29, 0x1, 0x1,

        // 0x0022 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        0x0d, 0x00, 0x02, 0x00, 0x22, 0x00, 0x03, 0x28, 0x1a, 0x23, 0x00, 0x4d, 0x2a,
        // 0x0023 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        // READ_ENCRYPTED, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x08, 0x00, 0x0b, 0xf5, 0x23, 0x00, 0x4d, 0x2a,
        // 0x0024 CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x0a, 0x00, 0x0f, 0xf1, 0x24, 0x00, 0x02, 0x29, 0x00, 0x00,
        // fixed report id = 2, type = Input (1) consumer
        // 0x0025 REPORT_REFERENCE-READ-2-1
        0x0a, 0x00, 0x02, 0x00, 0x25, 0x00, 0x08, 0x29, 0x2, 0x1,
};

constexpr std::uint8_t kAttMouseReport[] = {
        // 0x0026 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        0x0d, 0x00, 0x02, 0x00, 0x26, 0x00, 0x03, 0x28, 0x1a, 0x27, 0x00, 0x4d, 0x2a,
        // 0x0027 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        // READ_ENCRYPTED, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x08, 0x00, 0x0b, 0xf5, 0x27, 0x00, 0x4d, 0x2a,
        // 0x0028 CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x0a, 0x00, 0x0f, 0xf1, 0x28, 0x00, 0x02, 0x29, 0x00, 0x00,
        // fixed report id = 3, type = Input (1) mouse
        // 0x0029 REPORT_REFERENCE-READ-3-1
        0x0a, 0x00, 0x02, 0x00, 0x29, 0x00, 0x08, 0x29, 0x3, 0x1,
};

constexpr std::uint8_t kAttJoystickReport[] = {

        // 0x002a CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        0x0d, 0x00, 0x02, 0x00, 0x2a, 0x00, 0x03, 0x28, 0x1a, 0x2b, 0x00, 0x4d, 0x2a,
        // 0x002b VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | NOTIFY | ENCRYPTION_KEY_SIZE_16
        // READ_ENCRYPTED, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x08, 0x00, 0x0b, 0xf5, 0x2b, 0x00, 0x4d, 0x2a,
        // 0x002c CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x0a, 0x00, 0x0f, 0xf1, 0x2c, 0x00, 0x02, 0x29, 0x00, 0x00,
        // fixed report id = 4, type = Input (1) gamepad
        // 0x002d REPORT_REFERENCE-READ-4-1
        0x0a, 0x00, 0x02, 0x00, 0x2d, 0x00, 0x08, 0x29, 0x4, 0x1,
};

constexpr std::uint8_t kAttReportMapAndFeature[] = {
        // 0x002e CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | ENCRYPTION_KEY_SIZE_16
        0x0d, 0x00, 0x02, 0x00, 0x2e, 0x00, 0x03, 0x28, 0x0a, 0x2f, 0x00, 0x4d, 0x2a,
        // 0x002f VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT - DYNAMIC | READ | WRITE | ENCRYPTION_KEY_SIZE_16
        // READ_ENCRYPTED, WRITE_ENCRYPTED, ENCRYPTION_KEY_SIZE=16
        0x08, 0x00, 0x0b, 0xf5, 0x2f, 0x00, 0x4d, 0x2a,
        // fixed report id = 5, type = Feature (3)
        // 0x0030 REPORT_REFERENCE-READ-5-3
        0x0a, 0x00, 0x02, 0x00, 0x30, 0x00, 0x08, 0x29, 0x5, 0x3,
        // 0x0031 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT_MAP - DYNAMIC | READ
        0x0d, 0x00, 0x02, 0x00, 0x31, 0x00, 0x03, 0x28, 0x02, 0x32, 0x00, 0x4b, 0x2a,
        // 0x0032 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_REPORT_MAP - DYNAMIC | READ
        // READ_ANYBODY
        0x08, 0x00, 0x02, 0x01, 0x32, 0x00, 0x4b, 0x2a,
};

constexpr std::uint8_t kAttKeyboardBoot[] = {
        // 0x0033 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_KEYBOARD_INPUT_REPORT - DYNAMIC | READ | WRITE | NOTIFY
        0x0d, 0x00, 0x02, 0x00, 0x33, 0x00, 0x03, 0x28, 0x1a, 0x34, 0x00, 0x22, 0x2a,
        // 0x0034 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_KEYBOARD_INPUT_REPORT - DYNAMIC | READ | WRITE | NOTIFY
        // READ_ANYBODY, WRITE_ANYBODY
        0x08, 0x00, 0x0a, 0x01, 0x34, 0x00, 0x22, 0x2a,
        // 0x0035 CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ANYBODY
        0x0a, 0x00, 0x0e, 0x01, 0x35, 0x00, 0x02, 0x29, 0x00, 0x00,


        // 0x0036 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_KEYBOARD_OUTPUT_REPORT - DYNAMIC | READ | WRITE | WRITE_WITHOUT_RESPONSE
        0x0d, 0x00, 0x02, 0x00, 0x36, 0x00, 0x03, 0x28, 0x0e, 0x37, 0x00, 0x32, 0x2a,
        // 0x0037 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_KEYBOARD_OUTPUT_REPORT - DYNAMIC | READ | WRITE | WRITE_WITHOUT_RESPONSE
        // READ_ANYBODY, WRITE_ANYBODY
        0x08, 0x00, 0x0e, 0x01, 0x37, 0x00, 0x32, 0x2a,
};

constexpr std::uint8_t kAttMouseBoot[] = {
        // 0x0038 CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_MOUSE_INPUT_REPORT - DYNAMIC | READ | WRITE | NOTIFY
        0x0d, 0x00, 0x02, 0x00, 0x38, 0x00, 0x03, 0x28, 0x1a, 0x39, 0x00, 0x33, 0x2a,
        // 0x0039 VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_BOOT_MOUSE_INPUT_REPORT - DYNAMIC | READ | WRITE | NOTIFY
        // READ_ANYBODY, WRITE_ANYBODY
        0x08, 0x00, 0x0a, 0x01, 0x39, 0x00, 0x33, 0x2a,
        // 0x003a CLIENT_CHARACTERISTIC_CONFIGURATION
        // READ_ANYBODY, WRITE_ANYBODY
        0x0a, 0x00, 0x0e, 0x01, 0x3a, 0x00, 0x02, 0x29, 0x00, 0x00,
};

constexpr std::uint8_t kAttTail[] = {
        // bcdHID = 0x101 (v1.0.1), bCountryCode 0, remote wakeable = 0 | normally connectable 2
        // 0x003b CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HID_INFORMATION - READ
        0x0d, 0x00, 0x02, 0x00, 0x3b, 0x00, 0x03, 0x28, 0x02, 0x3c, 0x00, 0x4a, 0x2a,
        // 0x003c VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HID_INFORMATION - READ -'01 01 00 02'
        // READ_ANYBODY
        0x0c, 0x00, 0x02, 0x00, 0x3c, 0x00, 0x4a, 0x2a, 0x01, 0x01, 0x00, 0x02,
        // 0x003d CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HID_CONTROL_POINT - DYNAMIC | WRITE_WITHOUT_RESPONSE
        0x0d, 0x00, 0x02, 0x00, 0x3d, 0x00, 0x03, 0x28, 0x04, 0x3e, 0x00, 0x4c, 0x2a,
        // 0x003e VALUE CHARACTERISTIC-ORG_BLUETOOTH_CHARACTERISTIC_HID_CONTROL_POINT - DYNAMIC | WRITE_WITHOUT_RESPONSE
        // WRITE_ANYBODY
        0x08, 0x00, 0x04, 0x01, 0x3e, 0x00, 0x4c, 0x2a,
        // END
        0x00, 0x00,
};



hids_device_report_t gPhoneReportStorage[2] {};

std::int16_t encodeSignedAxis(std::int32_t value) {
    if (value <= std::numeric_limits<std::int32_t>::min()) {
        return static_cast<std::int16_t>(-32767);
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) * 32767ll /
        static_cast<std::int64_t>(
            std::numeric_limits<std::int32_t>::max()
        );

    return static_cast<std::int16_t>(
        std::clamp<std::int64_t>(scaled, -32767ll, 32767ll)
    );
}

std::int16_t encodeTriggerAxis(std::uint32_t value) {
    const std::uint64_t scaled =
        static_cast<std::uint64_t>(value) * 65534ull /
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
    report[offset + 1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    report[offset + 2] =
        static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    report[offset + 3] =
        static_cast<std::uint8_t>((value >> 24) & 0xFFu);
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
    storeLe16(report, 8, -32767);
    storeLe16(report, 10, -32767);

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
    const auto mapButton =
        [&](std::uint64_t sourceMask, std::uint8_t bit) {
            if ((state.buttons & sourceMask) != 0) {
                buttons |= static_cast<std::uint32_t>(1u << bit);
            }
        };

    mapButton(oag::ButtonSouth, 0);
    mapButton(oag::ButtonEast, 1);
    mapButton(oag::ButtonWest, 2);
    mapButton(oag::ButtonNorth, 3);
    mapButton(oag::ButtonLeftBumper, 4);
    mapButton(oag::ButtonRightBumper, 5);
    mapButton(oag::ButtonLeftStick, 6);
    mapButton(oag::ButtonRightStick, 7);
    mapButton(oag::ButtonBack, 8);
    mapButton(oag::ButtonStart, 9);
    mapButton(oag::ButtonGuide, 10);
    mapButton(oag::ButtonShare, 11);

    storeLe32(report, 13, buttons);
    return report;
}

} // namespace

namespace oag::firmware {

bool BlePhoneGamepadOutput::prepareAttDatabase() {
    if (prepared_) {
        return true;
    }

    std::size_t cursor = 0;
    const auto append =
        [&](const std::uint8_t* data, std::size_t length) -> bool {
            if (
                data == nullptr ||
                cursor + length > attDatabase_.size()
            ) {
                return false;
            }
            std::memcpy(attDatabase_.data() + cursor, data, length);
            cursor += length;
            return true;
        };

    if (!append(kAttHead, sizeof(kAttHead))) {
        return false;
    }

    constexpr std::size_t kNameLength = sizeof(kPhoneName) - 1;
    if (cursor + 8 + kNameLength > attDatabase_.size()) {
        return false;
    }

    attDatabase_[cursor++] =
        static_cast<std::uint8_t>(8 + kNameLength);
    attDatabase_[cursor++] = 0x00;
    attDatabase_[cursor++] = 0x02;
    attDatabase_[cursor++] = 0x00;
    attDatabase_[cursor++] = 0x03;
    attDatabase_[cursor++] = 0x00;
    attDatabase_[cursor++] = 0x00;
    attDatabase_[cursor++] = 0x2A;

    std::memcpy(
        attDatabase_.data() + cursor,
        kPhoneName,
        kNameLength
    );
    cursor += kNameLength;

    if (
        !append(kAttBatteryAndHidHead, sizeof(kAttBatteryAndHidHead)) ||
        !append(kAttKeyboardReports, sizeof(kAttKeyboardReports)) ||
        !append(kAttMouseReport, sizeof(kAttMouseReport)) ||
        !append(kAttJoystickReport, sizeof(kAttJoystickReport)) ||
        !append(kAttReportMapAndFeature, sizeof(kAttReportMapAndFeature)) ||
        !append(kAttKeyboardBoot, sizeof(kAttKeyboardBoot)) ||
        !append(kAttMouseBoot, sizeof(kAttMouseBoot)) ||
        !append(kAttTail, sizeof(kAttTail))
    ) {
        return false;
    }

    attDatabaseLength_ = cursor;
    prepared_ = true;
    return true;
}

const std::uint8_t* BlePhoneGamepadOutput::attDatabase() const {
    return prepared_ ? attDatabase_.data() : nullptr;
}

bool BlePhoneGamepadOutput::installDeviceServices() {
    if (servicesInstalled_) {
        return true;
    }
    if (!prepared_ || attDatabaseLength_ == 0) {
        return false;
    }

    gPhoneGamepadOutput = this;

    battery_service_server_init(100);
    device_information_service_server_init();

    hids_device_init_with_storage(
        0,
        kHidDescriptor,
        sizeof(kHidDescriptor),
        2,
        gPhoneReportStorage
    );

    hids_device_register_packet_handler(phoneHidsThunk);
    servicesInstalled_ = true;

    // Exact donor order: advertising is configured/enabled before HCI power.
    startAdvertising();
    return true;
}

void BlePhoneGamepadOutput::startAdvertising() {
    if (!servicesInstalled_ || connected()) {
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
        static_cast<std::uint8_t>(sizeof(kAdvertisingData)),
        const_cast<std::uint8_t*>(kAdvertisingData)
    );
    (void)gap_advertisements_enable(1);
}

bool BlePhoneGamepadOutput::adoptPeripheralConnection(
    std::uint16_t connectionHandle
) {
    if (
        connectionHandle == kInvalidHandle ||
        (
            connectionHandle_ != kInvalidHandle &&
            connectionHandle_ != connectionHandle
        )
    ) {
        return false;
    }

    connectionHandle_ = connectionHandle;
    inputSubscribed_ = false;
    canSendPending_ = false;
    subscriptionReadySignal_ = false;
    selfTestActive_ = false;
    selfTestStartedMs_ = 0;
    selfTestStep_ = 0;
    protocolMode_ = 1;
    reportDirty_ = true;
    return true;
}

bool BlePhoneGamepadOutput::ownsConnection(
    std::uint16_t connectionHandle
) const {
    return
        connectionHandle_ != kInvalidHandle &&
        connectionHandle_ == connectionHandle;
}

void BlePhoneGamepadOutput::handleDisconnection(
    std::uint16_t connectionHandle
) {
    if (!ownsConnection(connectionHandle)) {
        return;
    }

    connectionHandle_ = kInvalidHandle;
    inputSubscribed_ = false;
    canSendPending_ = false;
    subscriptionReadySignal_ = false;
    selfTestActive_ = false;
    selfTestStartedMs_ = 0;
    selfTestStep_ = 0;
    protocolMode_ = 1;
    reportDirty_ = true;

    startAdvertising();
}

bool BlePhoneGamepadOutput::connected() const {
    return connectionHandle_ != kInvalidHandle;
}

bool BlePhoneGamepadOutput::subscribed() const {
    return connected() && inputSubscribed_;
}

bool BlePhoneGamepadOutput::takeSubscriptionReadySignal() {
    const bool ready = subscriptionReadySignal_;
    subscriptionReadySignal_ = false;
    return ready;
}

void BlePhoneGamepadOutput::submit(
    const oag::LogicalGamepadState& state
) {
    const auto next = encodeReport(state);
    if (next == liveReport_) {
        return;
    }

    liveReport_ = next;
    if (selfTestActive_) {
        return;
    }

    if (report_ != liveReport_) {
        report_ = liveReport_;
        reportDirty_ = true;
        requestCanSend();
    }
}

void BlePhoneGamepadOutput::poll() {
    serviceConnectionSelfTest();

    if (
        reportDirty_ &&
        inputSubscribed_ &&
        connectionHandle_ != kInvalidHandle
    ) {
        requestCanSend();
    }
}

void BlePhoneGamepadOutput::startConnectionSelfTest() {
    if (
        !inputSubscribed_ ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    selfTestActive_ = true;
    selfTestStartedMs_ = btstack_run_loop_get_time_ms();
    selfTestStep_ = 0;

    report_.fill(0);
    storeLe16(report_, 8, -32767);
    storeLe16(report_, 10, -32767);
    storeLe32(report_, 13, 1u); // Button 1 DOWN
    reportDirty_ = true;
    requestCanSend();
}

void BlePhoneGamepadOutput::serviceConnectionSelfTest() {
    if (
        !selfTestActive_ ||
        !inputSubscribed_ ||
        connectionHandle_ == kInvalidHandle
    ) {
        return;
    }

    constexpr std::uint32_t kStepIntervalMs = 1200u;
    constexpr std::uint8_t kStepCount = 6u;

    const std::uint32_t elapsedMs =
        btstack_run_loop_get_time_ms() - selfTestStartedMs_;
    const std::uint8_t step =
        static_cast<std::uint8_t>(elapsedMs / kStepIntervalMs);

    if (step >= kStepCount) {
        selfTestActive_ = false;
        report_ = liveReport_;
        reportDirty_ = true;
        requestCanSend();
        return;
    }

    if (step == selfTestStep_ && elapsedMs >= kStepIntervalMs) {
        return;
    }

    if (step != selfTestStep_) {
        selfTestStep_ = step;
    } else if (elapsedMs != 0) {
        return;
    }

    std::array<std::uint8_t, 17> probe {};
    storeLe16(probe, 8, -32767);
    storeLe16(probe, 10, -32767);

    switch (step) {
        case 0:
            storeLe32(probe, 13, 1u);
            break;
        case 1:
            break;
        case 2:
            probe[12] = 5u;
            break;
        case 3:
            break;
        case 4:
            storeLe16(probe, 0, 32767);
            break;
        case 5:
        default:
            break;
    }

    if (probe != report_) {
        report_ = probe;
        reportDirty_ = true;
        requestCanSend();
    }
}

void BlePhoneGamepadOutput::requestCanSend() {
    if (
        !servicesInstalled_ ||
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

void BlePhoneGamepadOutput::sendCurrentReport() {
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

void BlePhoneGamepadOutput::handleHidsPacket(
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

    switch (
        hci_event_hids_meta_get_subevent_code(packet)
    ) {
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE: {
            const std::uint16_t handle =
                hids_subevent_input_report_enable_get_con_handle(packet);

            if (
                connectionHandle_ == kInvalidHandle &&
                !adoptPeripheralConnection(handle)
            ) {
                break;
            }
            if (handle != connectionHandle_) {
                break;
            }

            const bool wasSubscribed = inputSubscribed_;
            inputSubscribed_ =
                hids_subevent_input_report_enable_get_enable(packet) != 0;

            if (inputSubscribed_ && !wasSubscribed) {
                subscriptionReadySignal_ = true;
                startConnectionSelfTest();
            } else if (!inputSubscribed_) {
                selfTestActive_ = false;
                report_ = liveReport_;
                reportDirty_ = true;
            }
            break;
        }

        case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE: {
            const std::uint16_t handle =
                hids_subevent_boot_keyboard_input_report_enable_get_con_handle(
                    packet
                );
            if (connectionHandle_ == kInvalidHandle) {
                (void)adoptPeripheralConnection(handle);
            }
            break;
        }

        case HIDS_SUBEVENT_PROTOCOL_MODE:
            protocolMode_ =
                hids_subevent_protocol_mode_get_protocol_mode(packet);
            break;

        case HIDS_SUBEVENT_CAN_SEND_NOW: {
            const std::uint16_t handle =
                hids_subevent_can_send_now_get_con_handle(packet);
            if (handle != connectionHandle_) {
                break;
            }

            canSendPending_ = false;
            if (protocolMode_ == 1) {
                sendCurrentReport();
            }
            if (reportDirty_ && protocolMode_ == 1) {
                requestCanSend();
            }
            break;
        }

        default:
            break;
    }
}

} // namespace oag::firmware
