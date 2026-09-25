#include "oag/protocol/hid/generic_hid_gamepad_driver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oag {
namespace {

constexpr std::uint16_t kUsagePageGenericDesktop = 0x01;
constexpr std::uint16_t kUsagePageSimulation = 0x02;
constexpr std::uint16_t kUsagePageButton = 0x09;
constexpr std::uint16_t kUsagePageConsumer = 0x0C;

constexpr std::uint16_t kUsageJoystick = 0x04;
constexpr std::uint16_t kUsageGamepad = 0x05;
constexpr std::uint16_t kUsageMultiAxis = 0x08;

constexpr std::uint16_t kUsageX = 0x30;
constexpr std::uint16_t kUsageY = 0x31;
constexpr std::uint16_t kUsageZ = 0x32;
constexpr std::uint16_t kUsageRx = 0x33;
constexpr std::uint16_t kUsageRy = 0x34;
constexpr std::uint16_t kUsageRz = 0x35;
constexpr std::uint16_t kUsageHat = 0x39;
constexpr std::uint16_t kUsageDpadUp = 0x90;
constexpr std::uint16_t kUsageDpadDown = 0x91;
constexpr std::uint16_t kUsageDpadRight = 0x92;
constexpr std::uint16_t kUsageDpadLeft = 0x93;

constexpr std::uint16_t kUsageAccelerator = 0xC4;
constexpr std::uint16_t kUsageBrake = 0xC5;
constexpr std::uint16_t kUsageConsumerRecord = 0x00B2;
constexpr std::uint16_t kUsageConsumerHome = 0x0223;

constexpr std::size_t kMaxLocalUsages = 32;
constexpr std::size_t kGlobalStackDepth = 4;

struct GlobalState {
    std::uint16_t usagePage = 0;
    std::int32_t logicalMin = 0;
    std::int32_t logicalMax = 0;
    std::uint8_t reportSize = 0;
    std::uint8_t reportCount = 0;
    std::uint8_t reportId = 0;
};

struct LocalState {
    std::array<std::uint16_t, kMaxLocalUsages> usages {};
    std::uint8_t usageCount = 0;
    bool hasUsageMin = false;
    bool hasUsageMax = false;
    std::uint16_t usageMin = 0;
    std::uint16_t usageMax = 0;
};

void clearLocal(LocalState& local) {
    local = {};
}

std::uint32_t readUnsigned(
    const std::uint8_t* data,
    std::uint8_t size
) {
    std::uint32_t value = 0;

    for (std::uint8_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint32_t>(data[i]) << (8u * i);
    }

    return value;
}

std::int32_t signExtend(
    std::uint32_t value,
    std::uint8_t size
) {
    if (size == 0 || size >= 4) {
        return static_cast<std::int32_t>(value);
    }

    const std::uint8_t bits =
        static_cast<std::uint8_t>(size * 8u);
    const std::uint32_t sign = 1u << (bits - 1u);

    if ((value & sign) != 0) {
        value |= ~((1u << bits) - 1u);
    }

    return static_cast<std::int32_t>(value);
}

std::uint16_t localUsage(
    const LocalState& local,
    std::uint8_t index
) {
    if (index < local.usageCount) {
        return local.usages[index];
    }

    if (local.hasUsageMin && local.hasUsageMax) {
        const std::uint32_t candidate =
            static_cast<std::uint32_t>(local.usageMin) + index;

        if (candidate <= local.usageMax) {
            return static_cast<std::uint16_t>(candidate);
        }
    }

    if (local.usageCount != 0) {
        return local.usages[local.usageCount - 1];
    }

    return 0;
}

bool gamepadUsage(
    std::uint16_t page,
    std::uint16_t usage
) {
    return
        page == kUsagePageGenericDesktop &&
        (
            usage == kUsageJoystick ||
            usage == kUsageGamepad ||
            usage == kUsageMultiAxis
        );
}

GenericHidButtonLayout resolveButtonLayout(
    const GenericHidGamepadDescriptor& descriptor,
    const GenericHidGamepadQuirks& quirks
) {
    if (quirks.buttonLayout != GenericHidButtonLayout::Auto) {
        return quirks.buttonLayout;
    }

    bool hasRx = false;
    bool hasRy = false;
    bool hasUnsignedZ = false;
    bool hasUnsignedRz = false;
    bool hasAccelerator = false;
    bool hasBrake = false;

    const GenericHidButtonLayout buttonLayout =
        resolveButtonLayout(descriptor, quirks);

    for (std::uint8_t i = 0; i < descriptor.fieldCount; ++i) {
        const HidGamepadField& field = descriptor.fields[i];
        if (!field.used) continue;

        if (field.usagePage == kUsagePageGenericDesktop) {
            if (field.usage == kUsageRx) hasRx = true;
            else if (field.usage == kUsageRy) hasRy = true;
            else if (field.usage == kUsageZ) hasUnsignedZ = field.logicalMin >= 0;
            else if (field.usage == kUsageRz) hasUnsignedRz = field.logicalMin >= 0;
        } else if (field.usagePage == kUsagePageSimulation) {
            hasAccelerator = hasAccelerator || field.usage == kUsageAccelerator;
            hasBrake = hasBrake || field.usage == kUsageBrake;
        }
    }

    if (
        hasAccelerator ||
        hasBrake ||
        (hasRx && hasRy && hasUnsignedZ && hasUnsignedRz)
    ) {
        return GenericHidButtonLayout::ModernCanonical;
    }

    return GenericHidButtonLayout::LegacyDirectInput;
}

} // namespace

bool GenericHidGamepadDriver::parseDescriptor(
    const std::uint8_t* descriptor,
    std::size_t descriptorLength,
    const GenericHidGamepadQuirks& quirks,
    GenericHidGamepadDescriptor& output
) const {
    output = {};

    if (descriptor == nullptr || descriptorLength == 0) {
        return false;
    }

    GlobalState global {};
    std::array<GlobalState, kGlobalStackDepth> globalStack {};
    std::size_t globalStackCount = 0;

    LocalState local {};
    std::array<std::uint16_t, 256> reportBitOffsets {};

    std::uint8_t collectionDepth = 0;
    std::uint8_t gamepadCollectionDepth = 0;

    std::size_t offset = 0;

    while (offset < descriptorLength) {
        const std::uint8_t prefix = descriptor[offset++];

        if (prefix == 0xFE) {
            if (offset + 2 > descriptorLength) {
                return false;
            }

            const std::uint8_t longSize = descriptor[offset];
            offset += 2;

            if (offset + longSize > descriptorLength) {
                return false;
            }

            offset += longSize;
            continue;
        }

        const std::uint8_t sizeCode = prefix & 0x03u;
        const std::uint8_t dataSize =
            sizeCode == 3u ? 4u : sizeCode;
        const std::uint8_t type = (prefix >> 2u) & 0x03u;
        const std::uint8_t tag = (prefix >> 4u) & 0x0Fu;

        if (offset + dataSize > descriptorLength) {
            return false;
        }

        const std::uint8_t* data = descriptor + offset;
        const std::uint32_t unsignedValue =
            readUnsigned(data, dataSize);
        const std::int32_t signedValue =
            signExtend(unsignedValue, dataSize);

        offset += dataSize;

        if (type == 0) {
            // Input
            if (tag == 8) {
                std::uint16_t& bitOffset =
                    reportBitOffsets[global.reportId];

                const bool constant =
                    (unsignedValue & 0x01u) != 0;
                const bool variable =
                    (unsignedValue & 0x02u) != 0;

                if (
                    gamepadCollectionDepth != 0 &&
                    !constant &&
                    variable
                ) {
                    for (
                        std::uint8_t i = 0;
                        i < global.reportCount;
                        ++i
                    ) {
                        if (
                            output.fieldCount <
                                GenericHidGamepadDescriptor::kMaxFields &&
                            global.reportSize != 0 &&
                            global.reportSize <= 32
                        ) {
                            HidGamepadField& field =
                                output.fields[output.fieldCount++];

                            field.used = true;
                            field.reportId = global.reportId;
                            field.bitOffset = bitOffset;
                            field.bitSize = global.reportSize;
                            field.usagePage = global.usagePage;
                            field.usage = localUsage(local, i);
                            field.logicalMin = global.logicalMin;
                            field.logicalMax = global.logicalMax;

                            if (field.reportId != 0) {
                                output.usesReportIds = true;
                            }
                        }

                        bitOffset = static_cast<std::uint16_t>(
                            bitOffset + global.reportSize
                        );
                    }
                } else {
                    bitOffset = static_cast<std::uint16_t>(
                        bitOffset +
                        static_cast<std::uint16_t>(global.reportSize) *
                        static_cast<std::uint16_t>(global.reportCount)
                    );
                }

                clearLocal(local);
                continue;
            }

            // Collection
            if (tag == 10) {
                const std::uint16_t usage =
                    localUsage(local, 0);
                const std::uint8_t newDepth =
                    static_cast<std::uint8_t>(collectionDepth + 1u);

                if (
                    collectionDepth == 0 &&
                    (
                        gamepadUsage(global.usagePage, usage) ||
                        quirks.forceGamepad
                    )
                ) {
                    output.isGamepad = true;
                    output.topUsagePage =
                        quirks.forceGamepad && global.usagePage == 0
                            ? kUsagePageGenericDesktop
                            : global.usagePage;
                    output.topUsage =
                        quirks.forceGamepad && usage == 0
                            ? kUsageGamepad
                            : usage;
                    gamepadCollectionDepth = newDepth;
                }

                collectionDepth = newDepth;
                clearLocal(local);
                continue;
            }

            // End Collection
            if (tag == 12) {
                if (collectionDepth == gamepadCollectionDepth) {
                    gamepadCollectionDepth = 0;
                }

                if (collectionDepth > 0) {
                    --collectionDepth;
                }

                clearLocal(local);
                continue;
            }

            clearLocal(local);
            continue;
        }

        if (type == 1) {
            switch (tag) {
                case 0:
                    global.usagePage =
                        static_cast<std::uint16_t>(unsignedValue);
                    break;

                case 1:
                    global.logicalMin = signedValue;
                    break;

                case 2:
                    global.logicalMax =
                        global.logicalMin < 0
                            ? signedValue
                            : static_cast<std::int32_t>(unsignedValue);
                    break;

                case 7:
                    global.reportSize =
                        static_cast<std::uint8_t>(unsignedValue);
                    break;

                case 8:
                    global.reportId =
                        static_cast<std::uint8_t>(unsignedValue);
                    break;

                case 9:
                    global.reportCount =
                        static_cast<std::uint8_t>(unsignedValue);
                    break;

                case 10:
                    if (globalStackCount < globalStack.size()) {
                        globalStack[globalStackCount++] = global;
                    }
                    break;

                case 11:
                    if (globalStackCount > 0) {
                        global = globalStack[--globalStackCount];
                    }
                    break;

                default:
                    break;
            }

            continue;
        }

        if (type == 2) {
            switch (tag) {
                case 0:
                    if (local.usageCount < local.usages.size()) {
                        local.usages[local.usageCount++] =
                            static_cast<std::uint16_t>(unsignedValue);
                    }
                    break;

                case 1:
                    local.hasUsageMin = true;
                    local.usageMin =
                        static_cast<std::uint16_t>(unsignedValue);
                    break;

                case 2:
                    local.hasUsageMax = true;
                    local.usageMax =
                        static_cast<std::uint16_t>(unsignedValue);
                    break;

                default:
                    break;
            }
        }
    }

    output.valid = output.isGamepad && output.fieldCount != 0;
    return output.valid;
}

bool GenericHidGamepadDriver::looksLikeGamepadDescriptor(
    const std::uint8_t* descriptor,
    std::size_t descriptorLength
) const {
    if (descriptor == nullptr || descriptorLength == 0) {
        return false;
    }

    std::uint16_t usagePage = 0;
    bool hasX = false;
    bool hasY = false;
    bool hasHat = false;
    bool hasButtons = false;
    std::uint8_t axisCount = 0;

    std::size_t offset = 0;

    while (offset < descriptorLength) {
        const std::uint8_t prefix = descriptor[offset++];

        if (prefix == 0xFE) {
            if (offset + 2 > descriptorLength) {
                return false;
            }

            const std::uint8_t longSize = descriptor[offset];
            offset += 2;

            if (offset + longSize > descriptorLength) {
                return false;
            }

            offset += longSize;
            continue;
        }

        const std::uint8_t sizeCode = prefix & 0x03u;
        const std::uint8_t dataSize =
            sizeCode == 3u ? 4u : sizeCode;
        const std::uint8_t type = (prefix >> 2u) & 0x03u;
        const std::uint8_t tag = (prefix >> 4u) & 0x0Fu;

        if (offset + dataSize > descriptorLength) {
            return false;
        }

        const std::uint32_t value =
            readUnsigned(descriptor + offset, dataSize);
        offset += dataSize;

        if (type == 1 && tag == 0) {
            usagePage = static_cast<std::uint16_t>(value);
            continue;
        }

        if (type != 2) {
            continue;
        }

        if (usagePage == kUsagePageButton) {
            if (tag == 0 || tag == 1) {
                hasButtons = true;
            }
            continue;
        }

        if (usagePage != kUsagePageGenericDesktop || tag != 0) {
            continue;
        }

        const std::uint16_t usage =
            static_cast<std::uint16_t>(value);

        switch (usage) {
            case kUsageX:
                hasX = true;
                ++axisCount;
                break;
            case kUsageY:
                hasY = true;
                ++axisCount;
                break;
            case kUsageZ:
            case kUsageRx:
            case kUsageRy:
            case kUsageRz:
                ++axisCount;
                break;
            case kUsageHat:
                hasHat = true;
                break;
            default:
                break;
        }
    }

    return
        hasX &&
        hasY &&
        (hasButtons || hasHat || axisCount >= 4);
}

bool GenericHidGamepadDriver::parseReport(
    DeviceId source,
    const GenericHidGamepadDescriptor& descriptor,
    const GenericHidGamepadQuirks& quirks,
    const std::uint8_t* report,
    std::size_t reportLength,
    std::uint64_t timestampUs,
    UniversalGamepadState& output
) const {
    if (
        !source.valid() ||
        !descriptor.valid ||
        !descriptor.isGamepad ||
        descriptor.fieldCount == 0 ||
        report == nullptr ||
        reportLength == 0
    ) {
        return false;
    }

    std::uint8_t incomingReportId = 0;
    const std::uint8_t* payload = report;
    std::size_t payloadLength = reportLength;

    if (descriptor.usesReportIds) {
        incomingReportId = report[0];
        payload = report + 1;
        --payloadLength;

        if (payloadLength == 0) {
            return false;
        }
    }

    UniversalGamepadState next {};
    next.source = source;
    next.connected = true;
    next.generation = output.generation + 1u;
    next.timestampUs = timestampUs;

    bool hasX = false;
    bool hasY = false;
    bool hasRx = false;
    bool hasRy = false;
    bool hasZ = false;
    bool hasRz = false;

    std::int32_t zValue = 0;
    std::int32_t zMin = 0;
    std::int32_t zMax = 0;

    std::int32_t rzValue = 0;
    std::int32_t rzMin = 0;
    std::int32_t rzMax = 0;

    bool hasAccelerator = false;
    bool hasBrake = false;

    std::int32_t acceleratorValue = 0;
    std::int32_t acceleratorMin = 0;
    std::int32_t acceleratorMax = 0;

    std::int32_t brakeValue = 0;
    std::int32_t brakeMin = 0;
    std::int32_t brakeMax = 0;

    for (std::uint8_t i = 0; i < descriptor.fieldCount; ++i) {
        const HidGamepadField& field = descriptor.fields[i];

        if (
            !field.used ||
            field.reportId != incomingReportId
        ) {
            continue;
        }

        const std::uint32_t raw = extractBits(
            payload,
            payloadLength,
            field.bitOffset,
            field.bitSize
        );

        const std::int32_t value =
            signedFieldValue(field, raw);

        if (field.usagePage == kUsagePageButton) {
            applyButton(
                next,
                buttonLayout,
                field.usage,
                value != 0
            );
            continue;
        }

        if (field.usagePage == kUsagePageSimulation) {
            switch (field.usage) {
                case kUsageAccelerator:
                    hasAccelerator = true;
                    acceleratorValue = value;
                    acceleratorMin = field.logicalMin;
                    acceleratorMax = field.logicalMax;
                    break;

                case kUsageBrake:
                    hasBrake = true;
                    brakeValue = value;
                    brakeMin = field.logicalMin;
                    brakeMax = field.logicalMax;
                    break;

                default:
                    break;
            }
            continue;
        }

        if (field.usagePage == kUsagePageConsumer) {
            if (value != 0 && field.usage == kUsageConsumerHome) {
                next.buttons |= ButtonGuide;
            } else if (
                value != 0 &&
                field.usage == kUsageConsumerRecord
            ) {
                next.buttons |= ButtonShare;
            }
            continue;
        }

        if (field.usagePage != kUsagePageGenericDesktop) {
            continue;
        }

        switch (field.usage) {
            case kUsageX:
                next.lx = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasX = true;
                break;

            case kUsageY:
                next.ly = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasY = true;
                break;

            case kUsageRx:
                next.rx = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasRx = true;
                break;

            case kUsageRy:
                next.ry = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasRy = true;
                break;

            case kUsageZ:
                hasZ = true;
                zValue = value;
                zMin = field.logicalMin;
                zMax = field.logicalMax;
                break;

            case kUsageRz:
                hasRz = true;
                rzValue = value;
                rzMin = field.logicalMin;
                rzMax = field.logicalMax;
                break;

            case kUsageHat:
                next.dpad = hatToDpad(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                break;

            case kUsageDpadUp:
                if (value != 0) next.dpad |= static_cast<std::uint8_t>(DpadBits::Up);
                break;
            case kUsageDpadDown:
                if (value != 0) next.dpad |= static_cast<std::uint8_t>(DpadBits::Down);
                break;
            case kUsageDpadRight:
                if (value != 0) next.dpad |= static_cast<std::uint8_t>(DpadBits::Right);
                break;
            case kUsageDpadLeft:
                if (value != 0) next.dpad |= static_cast<std::uint8_t>(DpadBits::Left);
                break;

            default:
                break;
        }
    }

    const bool zrRightStick =
        hasZ &&
        hasRz &&
        !hasRx &&
        !hasRy &&
        (
            quirks.zRzAsRightStick ||
            (zMin < 0 && rzMin < 0) ||
            hasAccelerator ||
            hasBrake
        );

    if (zrRightStick) {
        next.rx = scaleAxis(zValue, zMin, zMax);
        next.ry = scaleAxis(rzValue, rzMin, rzMax);
        hasRx = true;
        hasRy = true;
    }

    if (hasBrake) {
        next.leftTrigger = scaleTrigger(
            brakeValue,
            brakeMin,
            brakeMax
        );
    } else if (hasZ && !hasRx && zMin >= 0) {
        next.leftTrigger = scaleTrigger(
            zValue,
            zMin,
            zMax
        );
    }

    if (hasAccelerator) {
        next.rightTrigger = scaleTrigger(
            acceleratorValue,
            acceleratorMin,
            acceleratorMax
        );
    } else if (hasRz && !hasRy && rzMin >= 0) {
        next.rightTrigger = scaleTrigger(
            rzValue,
            rzMin,
            rzMax
        );
    }

    const bool hasUsefulField =
        hasX ||
        hasY ||
        hasRx ||
        hasRy ||
        hasZ ||
        hasRz ||
        hasAccelerator ||
        hasBrake ||
        next.dpad != 0 ||
        next.buttons != 0;

    if (!hasUsefulField && output.generation == 0) {
        return false;
    }

    output = next;
    return true;
}

std::uint32_t GenericHidGamepadDriver::extractBits(
    const std::uint8_t* data,
    std::size_t dataLength,
    std::uint16_t bitOffset,
    std::uint8_t bitSize
) {
    if (
        data == nullptr ||
        bitSize == 0 ||
        bitSize > 32
    ) {
        return 0;
    }

    std::uint32_t value = 0;

    for (std::uint8_t bit = 0; bit < bitSize; ++bit) {
        const std::uint32_t absoluteBit =
            static_cast<std::uint32_t>(bitOffset) + bit;
        const std::size_t byteIndex =
            absoluteBit / 8u;

        if (byteIndex >= dataLength) {
            break;
        }

        const std::uint8_t bitIndex =
            static_cast<std::uint8_t>(absoluteBit % 8u);

        if ((data[byteIndex] & (1u << bitIndex)) != 0) {
            value |= 1u << bit;
        }
    }

    return value;
}

std::int32_t GenericHidGamepadDriver::signedFieldValue(
    const HidGamepadField& field,
    std::uint32_t raw
) {
    if (
        field.logicalMin >= 0 ||
        field.bitSize >= 32
    ) {
        return static_cast<std::int32_t>(raw);
    }

    const std::uint32_t sign =
        1u << (field.bitSize - 1u);

    if ((raw & sign) != 0) {
        raw |= ~((1u << field.bitSize) - 1u);
    }

    return static_cast<std::int32_t>(raw);
}

std::int32_t GenericHidGamepadDriver::scaleAxis(
    std::int32_t value,
    std::int32_t logicalMin,
    std::int32_t logicalMax
) {
    if (logicalMax <= logicalMin) {
        return 0;
    }

    value = std::clamp(value, logicalMin, logicalMax);

    const std::int64_t range =
        static_cast<std::int64_t>(logicalMax) - logicalMin;
    const std::int64_t offset =
        static_cast<std::int64_t>(value) - logicalMin;

    const std::int64_t signedSpan =
        static_cast<std::int64_t>(
            std::numeric_limits<std::uint32_t>::max()
        );

    const std::int64_t scaled =
        (offset * signedSpan) / range +
        std::numeric_limits<std::int32_t>::min();

    if (scaled <= std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }

    if (scaled >= std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }

    return static_cast<std::int32_t>(scaled);
}

std::uint32_t GenericHidGamepadDriver::scaleTrigger(
    std::int32_t value,
    std::int32_t logicalMin,
    std::int32_t logicalMax
) {
    if (logicalMax <= logicalMin) {
        return 0;
    }

    value = std::clamp(value, logicalMin, logicalMax);

    const std::uint64_t range =
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(logicalMax) - logicalMin
        );

    const std::uint64_t offset =
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value) - logicalMin
        );

    return static_cast<std::uint32_t>(
        (offset * std::numeric_limits<std::uint32_t>::max()) /
        range
    );
}

std::uint8_t GenericHidGamepadDriver::hatToDpad(
    std::int32_t value,
    std::int32_t logicalMin,
    std::int32_t logicalMax
) {
    std::int32_t index = -1;

    if (
        logicalMin == 0 &&
        logicalMax >= 7 &&
        value >= 0 &&
        value <= 7
    ) {
        index = value;
    } else if (
        logicalMin == 1 &&
        logicalMax >= 8 &&
        value >= 1 &&
        value <= 8
    ) {
        index = value - 1;
    } else if (
        value >= logicalMin &&
        value <= logicalMax &&
        logicalMax > logicalMin
    ) {
        const std::int32_t range =
            logicalMax - logicalMin + 1;

        index =
            ((value - logicalMin) * 8) / range;
    }

    switch (index) {
        case 0:
            return static_cast<std::uint8_t>(DpadBits::Up);
        case 1:
            return
                static_cast<std::uint8_t>(DpadBits::Up) |
                static_cast<std::uint8_t>(DpadBits::Right);
        case 2:
            return static_cast<std::uint8_t>(DpadBits::Right);
        case 3:
            return
                static_cast<std::uint8_t>(DpadBits::Down) |
                static_cast<std::uint8_t>(DpadBits::Right);
        case 4:
            return static_cast<std::uint8_t>(DpadBits::Down);
        case 5:
            return
                static_cast<std::uint8_t>(DpadBits::Down) |
                static_cast<std::uint8_t>(DpadBits::Left);
        case 6:
            return static_cast<std::uint8_t>(DpadBits::Left);
        case 7:
            return
                static_cast<std::uint8_t>(DpadBits::Up) |
                static_cast<std::uint8_t>(DpadBits::Left);
        default:
            return 0;
    }
}

void GenericHidGamepadDriver::applyButton(
    UniversalGamepadState& state,
    GenericHidButtonLayout layout,
    std::uint16_t usage,
    bool pressed
) {
    if (!pressed) return;

    if (layout == GenericHidButtonLayout::SonyPlayStation) {
        switch (usage) {
            case 1: state.buttons |= ButtonWest; break;
            case 2: state.buttons |= ButtonSouth; break;
            case 3: state.buttons |= ButtonEast; break;
            case 4: state.buttons |= ButtonNorth; break;
            case 5: state.buttons |= ButtonLeftBumper; break;
            case 6: state.buttons |= ButtonRightBumper; break;
            case 7: state.leftTrigger = std::numeric_limits<std::uint32_t>::max(); break;
            case 8: state.rightTrigger = std::numeric_limits<std::uint32_t>::max(); break;
            case 9: state.buttons |= ButtonBack; break;
            case 10: state.buttons |= ButtonStart; break;
            case 11: state.buttons |= ButtonLeftStick; break;
            case 12: state.buttons |= ButtonRightStick; break;
            case 13: state.buttons |= ButtonGuide; break;
            case 14: state.buttons |= ButtonShare; break;
            default: break;
        }
        return;
    }

    switch (usage) {
        case 1: state.buttons |= ButtonSouth; return;
        case 2: state.buttons |= ButtonEast; return;
        case 3: state.buttons |= ButtonWest; return;
        case 4: state.buttons |= ButtonNorth; return;
        case 5: state.buttons |= ButtonLeftBumper; return;
        case 6: state.buttons |= ButtonRightBumper; return;
        default: break;
    }

    if (layout == GenericHidButtonLayout::ModernCanonical) {
        switch (usage) {
            case 7: state.buttons |= ButtonBack; break;
            case 8: state.buttons |= ButtonStart; break;
            case 9: state.buttons |= ButtonLeftStick; break;
            case 10: state.buttons |= ButtonRightStick; break;
            case 11: state.buttons |= ButtonGuide; break;
            case 12: state.buttons |= ButtonShare; break;
            default: break;
        }
        return;
    }

    switch (usage) {
        case 7: state.leftTrigger = std::numeric_limits<std::uint32_t>::max(); break;
        case 8: state.rightTrigger = std::numeric_limits<std::uint32_t>::max(); break;
        case 9: state.buttons |= ButtonBack; break;
        case 10: state.buttons |= ButtonStart; break;
        case 11: state.buttons |= ButtonLeftStick; break;
        case 12: state.buttons |= ButtonRightStick; break;
        case 13: state.buttons |= ButtonGuide; break;
        case 14: state.buttons |= ButtonShare; break;
        default: break;
    }
}

} // namespace oag
