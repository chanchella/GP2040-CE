#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/device/device_id.h"
#include "oag/input/gamepad_state.h"

namespace oag {

struct HidGamepadField {
    bool used = false;
    std::uint8_t reportId = 0;
    std::uint16_t bitOffset = 0;
    std::uint8_t bitSize = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::int32_t logicalMin = 0;
    std::int32_t logicalMax = 0;
};

struct GenericHidGamepadDescriptor {
    static constexpr std::size_t kMaxFields = 96;

    bool valid = false;
    bool isGamepad = false;
    bool usesReportIds = false;
    std::uint16_t topUsagePage = 0;
    std::uint16_t topUsage = 0;
    std::uint8_t fieldCount = 0;
    std::array<HidGamepadField, kMaxFields> fields {};
};

enum class GenericHidButtonLayout : std::uint8_t {
    Auto = 0,
    LegacyDirectInput,
    ModernCanonical,
    SonyPlayStation,
};

struct GenericHidGamepadQuirks {
    bool forceGamepad = false;
    bool zRzAsRightStick = false;
    GenericHidButtonLayout buttonLayout =
        GenericHidButtonLayout::Auto;
};

class GenericHidGamepadDriver {
public:
    bool parseDescriptor(
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        const GenericHidGamepadQuirks& quirks,
        GenericHidGamepadDescriptor& output
    ) const;

    bool looksLikeGamepadDescriptor(
        const std::uint8_t* descriptor,
        std::size_t descriptorLength
    ) const;

    bool parseReport(
        DeviceId source,
        const GenericHidGamepadDescriptor& descriptor,
        const GenericHidGamepadQuirks& quirks,
        const std::uint8_t* report,
        std::size_t reportLength,
        std::uint64_t timestampUs,
        UniversalGamepadState& output
    ) const;

private:
    static std::uint32_t extractBits(
        const std::uint8_t* data,
        std::size_t dataLength,
        std::uint16_t bitOffset,
        std::uint8_t bitSize
    );

    static std::int32_t signedFieldValue(
        const HidGamepadField& field,
        std::uint32_t raw
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

    static void applyButton(
        UniversalGamepadState& state,
        GenericHidButtonLayout layout,
        std::uint16_t usage,
        bool pressed
    );
};

} // namespace oag
