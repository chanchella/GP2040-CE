#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/device/device_registry.h"
#include "oag/input/gamepad_state.h"
#include "oag/input/keyboard_state.h"
#include "oag/input/mouse_state.h"

namespace oag::firmware {

enum class BluetoothGamepadProfileV2 : std::uint8_t {
    GenericHid = 0,
    XboxBle,
    SonyDs4Classic,
    SonyDualSenseClassic,
    NintendoSwitchClassic,
};

struct BluetoothHidFieldRangeV2 {
    bool used = false;
    std::uint16_t reportId = 0xFFFFu;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::int32_t logicalMin = 0;
    std::int32_t logicalMax = 0;
};

struct BluetoothHidReportMetaV2 {
    bool used = false;
    std::uint16_t reportId = 0xFFFFu;
    std::uint8_t flags = 0;
};

struct BluetoothHidDescriptorV2 {
    static constexpr std::size_t kMaxFields = 96;
    static constexpr std::size_t kMaxReports = 16;

    bool valid = false;
    bool hasGamepad = false;
    bool hasKeyboard = false;
    bool hasMouse = false;
    bool usesReportIds = false;
    bool xboxBleLayout = false;

    BluetoothGamepadProfileV2 profile =
        BluetoothGamepadProfileV2::GenericHid;

    std::uint8_t fieldCount = 0;
    std::uint8_t reportMetaCount = 0;

    std::array<
        BluetoothHidFieldRangeV2,
        kMaxFields
    > fields {};

    std::array<
        BluetoothHidReportMetaV2,
        kMaxReports
    > reports {};
};

class BluetoothHidParserV2 {
public:
    bool parseDescriptor(
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        BluetoothHidDescriptorV2& output
    ) const;

    bool parseGamepad(
        DeviceId source,
        TransportType transport,
        const BluetoothHidDescriptorV2& info,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        const std::uint8_t* report,
        std::size_t reportLength,
        std::uint64_t timestampUs,
        UniversalGamepadState& output
    ) const;

    bool parseKeyboard(
        DeviceId source,
        const BluetoothHidDescriptorV2& info,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        const std::uint8_t* report,
        std::size_t reportLength,
        std::uint64_t timestampUs,
        KeyboardState& output
    ) const;

    bool parseMouse(
        DeviceId source,
        const BluetoothHidDescriptorV2& info,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        const std::uint8_t* report,
        std::size_t reportLength,
        std::uint64_t timestampUs,
        MouseState& output
    ) const;

private:
    static constexpr std::uint8_t kReportKeyboard = 1u << 0;
    static constexpr std::uint8_t kReportButtons = 1u << 1;
    static constexpr std::uint8_t kReportPointer = 1u << 2;

    static BluetoothHidReportMetaV2* ensureReportMeta(
        BluetoothHidDescriptorV2& info,
        std::uint16_t reportId
    );

    static const BluetoothHidReportMetaV2* findReportMeta(
        const BluetoothHidDescriptorV2& info,
        std::uint16_t reportId
    );

    static std::uint16_t incomingReportId(
        const BluetoothHidDescriptorV2& info,
        const std::uint8_t* report,
        std::size_t reportLength
    );

    static bool findFieldRange(
        const BluetoothHidDescriptorV2& info,
        std::uint16_t reportId,
        std::uint16_t usagePage,
        std::uint16_t usage,
        std::int32_t& logicalMin,
        std::int32_t& logicalMax
    );

    static std::int32_t scaleAxis(
        std::int32_t value,
        std::int32_t logicalMin,
        std::int32_t logicalMax
    );

    static std::uint32_t scaleTrigger(
        std::int32_t value,
        std::int32_t logicalMin,
        std::int32_t logicalMax
    );

    static std::uint8_t hatToDpad(
        std::int32_t value,
        std::int32_t logicalMin,
        std::int32_t logicalMax
    );

    static BluetoothGamepadProfileV2 effectiveProfile(
        TransportType transport,
        BluetoothGamepadProfileV2 descriptorProfile,
        const std::uint8_t* report,
        std::size_t reportLength
    );

    static void applyButton(
        BluetoothGamepadProfileV2 profile,
        std::uint16_t usage,
        std::uint64_t& buttons,
        bool& digitalLeftTrigger,
        bool& digitalRightTrigger
    );
};

} // namespace oag::firmware
