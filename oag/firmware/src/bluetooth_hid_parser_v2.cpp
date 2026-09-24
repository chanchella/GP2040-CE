#include "oag/firmware/bluetooth_hid_parser_v2.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "btstack.h"

namespace oag::firmware {
namespace {

void classifyTopLevelApplications(
    BluetoothHidDescriptorV2& info,
    const std::uint8_t* descriptor,
    std::size_t descriptorLength
) {
    std::uint32_t usagePage = 0;
    std::uint32_t localUsage = 0;
    std::size_t offset = 0;

    while (offset < descriptorLength) {
        const std::uint8_t prefix = descriptor[offset++];

        if (prefix == 0xFE) {
            if (offset + 2 > descriptorLength) {
                break;
            }

            const std::uint8_t longSize = descriptor[offset];
            offset += static_cast<std::size_t>(2u + longSize);
            continue;
        }

        std::uint8_t dataSize = prefix & 0x03u;
        if (dataSize == 3u) {
            dataSize = 4u;
        }

        if (offset + dataSize > descriptorLength) {
            break;
        }

        std::uint32_t value = 0;
        for (std::uint8_t i = 0; i < dataSize; ++i) {
            value |=
                static_cast<std::uint32_t>(descriptor[offset + i]) <<
                (8u * i);
        }

        offset += dataSize;

        const std::uint8_t type = (prefix >> 2u) & 0x03u;
        const std::uint8_t tag = (prefix >> 4u) & 0x0Fu;

        if (type == 1u && tag == 0u) {
            usagePage = value;
            continue;
        }

        if (type == 2u && tag == 0u) {
            localUsage = value;
            continue;
        }

        if (
            type == 0u &&
            tag == 0x0Au &&
            value == 0x01u &&
            usagePage == USAGE_PAGE_GENERIC_DESKTOP
        ) {
            switch (localUsage) {
                case USAGE_DESKTOP_GAMEPAD:
                case USAGE_DESKTOP_JOYSTICK:
                    info.hasGamepad = true;
                    break;

                case USAGE_DESKTOP_KEYBOARD:
                    info.hasKeyboard = true;
                    break;

                case USAGE_DESKTOP_MOUSE:
                    info.hasMouse = true;
                    break;

                default:
                    break;
            }
        }

        if (type == 0u) {
            localUsage = 0;
        }
    }
}

} // namespace

BluetoothHidReportMetaV2*
BluetoothHidParserV2::ensureReportMeta(
    BluetoothHidDescriptorV2& info,
    std::uint16_t reportId
) {
    for (std::size_t i = 0; i < info.reportMetaCount; ++i) {
        if (
            info.reports[i].used &&
            info.reports[i].reportId == reportId
        ) {
            return &info.reports[i];
        }
    }

    if (info.reportMetaCount >= info.reports.size()) {
        return nullptr;
    }

    BluetoothHidReportMetaV2& meta =
        info.reports[info.reportMetaCount++];

    meta = {};
    meta.used = true;
    meta.reportId = reportId;
    return &meta;
}

const BluetoothHidReportMetaV2*
BluetoothHidParserV2::findReportMeta(
    const BluetoothHidDescriptorV2& info,
    std::uint16_t reportId
) {
    for (std::size_t i = 0; i < info.reportMetaCount; ++i) {
        if (
            info.reports[i].used &&
            info.reports[i].reportId == reportId
        ) {
            return &info.reports[i];
        }
    }

    return nullptr;
}

std::uint16_t BluetoothHidParserV2::incomingReportId(
    const BluetoothHidDescriptorV2& info,
    const std::uint8_t* report,
    std::size_t reportLength
) {
    if (
        !info.usesReportIds ||
        report == nullptr ||
        reportLength == 0
    ) {
        return HID_REPORT_ID_UNDEFINED;
    }

    return report[0];
}

bool BluetoothHidParserV2::parseDescriptor(
    const std::uint8_t* descriptor,
    std::size_t descriptorLength,
    BluetoothHidDescriptorV2& output
) const {
    output = {};

    if (
        descriptor == nullptr ||
        descriptorLength == 0 ||
        descriptorLength > std::numeric_limits<std::uint16_t>::max()
    ) {
        return false;
    }

    classifyTopLevelApplications(
        output,
        descriptor,
        descriptorLength
    );

    output.usesReportIds =
        btstack_hid_report_id_declared(
            descriptor,
            static_cast<std::uint16_t>(descriptorLength)
        );

    bool hasAxis = false;
    bool hasButtons = false;
    bool hasKeyboardFields = false;
    bool hasAccelerator = false;
    bool hasBrake = false;

    bool xboxButton1 = false;
    bool xboxButton2 = false;
    bool xboxButton4 = false;
    bool xboxButton5 = false;
    bool xboxButton7 = false;
    bool xboxButton8 = false;
    bool xboxButton11 = false;
    bool xboxButton12 = false;
    bool xboxButton13 = false;
    bool xboxButton14 = false;
    bool xboxButton15 = false;

    btstack_hid_usage_iterator_t iterator {};
    btstack_hid_usage_iterator_init(
        &iterator,
        descriptor,
        static_cast<std::uint16_t>(descriptorLength),
        BT_HID_REPORT_TYPE_INPUT
    );

    while (btstack_hid_usage_iterator_has_more(&iterator)) {
        const std::int32_t logicalMin =
            iterator.global_logical_minimum;
        const std::int32_t logicalMax =
            iterator.global_logical_maximum;

        btstack_hid_usage_item_t item {};
        btstack_hid_usage_iterator_get_item(
            &iterator,
            &item
        );

        if (item.usage_page == 0 || item.usage == 0) {
            continue;
        }

        if (output.fieldCount < output.fields.size()) {
            BluetoothHidFieldRangeV2& field =
                output.fields[output.fieldCount++];

            field.used = true;
            field.reportId = item.report_id;
            field.usagePage = item.usage_page;
            field.usage = item.usage;
            field.logicalMin = logicalMin;
            field.logicalMax = logicalMax;
        }

        BluetoothHidReportMetaV2* meta =
            ensureReportMeta(output, item.report_id);

        if (item.usage_page == USAGE_PAGE_KEYBOARD) {
            hasKeyboardFields = true;
            if (meta != nullptr) {
                meta->flags |= kReportKeyboard;
            }
        }

        if (item.usage_page == USAGE_PAGE_BUTTON) {
            hasButtons = true;
            if (meta != nullptr) {
                meta->flags |= kReportButtons;
            }

            switch (item.usage) {
                case 1: xboxButton1 = true; break;
                case 2: xboxButton2 = true; break;
                case 4: xboxButton4 = true; break;
                case 5: xboxButton5 = true; break;
                case 7: xboxButton7 = true; break;
                case 8: xboxButton8 = true; break;
                case 11: xboxButton11 = true; break;
                case 12: xboxButton12 = true; break;
                case 13: xboxButton13 = true; break;
                case 14: xboxButton14 = true; break;
                case 15: xboxButton15 = true; break;
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

        if (item.usage_page == USAGE_PAGE_GENERIC_DESKTOP) {
            switch (item.usage) {
                case USAGE_X:
                case USAGE_Y:
                case USAGE_Z:
                case USAGE_RX:
                case USAGE_RY:
                case USAGE_RZ:
                case USAGE_WHEEL:
                case USAGE_HAT:
                case USAGE_DPAD_UP:
                case USAGE_DPAD_DOWN:
                case USAGE_DPAD_LEFT:
                case USAGE_DPAD_RIGHT:
                    hasAxis = true;
                    break;

                default:
                    break;
            }

            if (
                meta != nullptr &&
                (
                    item.usage == USAGE_X ||
                    item.usage == USAGE_Y ||
                    item.usage == USAGE_WHEEL
                )
            ) {
                meta->flags |= kReportPointer;
            }
        }

        if (
            item.usage_page == USAGE_PAGE_CONSUMER &&
            item.usage == USAGE_CONSUMER_AC_PAN &&
            meta != nullptr
        ) {
            meta->flags |= kReportPointer;
        }
    }

    output.hasKeyboard =
        output.hasKeyboard ||
        hasKeyboardFields;

    if (
        !output.hasGamepad &&
        !output.hasKeyboard &&
        !output.hasMouse &&
        hasAxis &&
        hasButtons
    ) {
        output.hasGamepad = true;
    }

    output.xboxBleLayout =
        output.hasGamepad &&
        hasAccelerator &&
        hasBrake &&
        xboxButton1 &&
        xboxButton2 &&
        xboxButton4 &&
        xboxButton5 &&
        xboxButton7 &&
        xboxButton8 &&
        xboxButton11 &&
        xboxButton12 &&
        xboxButton13 &&
        xboxButton14 &&
        xboxButton15;

    if (output.xboxBleLayout) {
        output.profile = BluetoothGamepadProfileV2::XboxBle;
    }

    output.valid =
        output.hasGamepad ||
        output.hasKeyboard ||
        output.hasMouse;

    return output.valid;
}

bool BluetoothHidParserV2::findFieldRange(
    const BluetoothHidDescriptorV2& info,
    std::uint16_t reportId,
    std::uint16_t usagePage,
    std::uint16_t usage,
    std::int32_t& logicalMin,
    std::int32_t& logicalMax
) {
    for (std::size_t i = 0; i < info.fieldCount; ++i) {
        const BluetoothHidFieldRangeV2& field = info.fields[i];

        if (
            field.used &&
            field.usagePage == usagePage &&
            field.usage == usage &&
            (
                field.reportId == reportId ||
                field.reportId == HID_REPORT_ID_UNDEFINED
            )
        ) {
            logicalMin = field.logicalMin;
            logicalMax = field.logicalMax;
            return true;
        }
    }

    logicalMin = 0;
    logicalMax = 0;
    return false;
}

std::int32_t BluetoothHidParserV2::scaleAxis(
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

std::uint32_t BluetoothHidParserV2::scaleTrigger(
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

std::uint8_t BluetoothHidParserV2::hatToDpad(
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
        index =
            ((value - logicalMin) * 8) /
            (logicalMax - logicalMin + 1);
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

BluetoothGamepadProfileV2
BluetoothHidParserV2::effectiveProfile(
    TransportType transport,
    BluetoothGamepadProfileV2 descriptorProfile,
    const std::uint8_t* report,
    std::size_t reportLength
) {
    if (
        transport != TransportType::BluetoothClassic ||
        report == nullptr ||
        reportLength == 0
    ) {
        return descriptorProfile;
    }

    if (report[0] == 0x31 && reportLength >= 70) {
        return BluetoothGamepadProfileV2::SonyDualSenseClassic;
    }

    if (
        (report[0] == 0x11 && reportLength >= 70) ||
        (report[0] == 0x01 && reportLength == 10)
    ) {
        return BluetoothGamepadProfileV2::SonyDs4Classic;
    }

    if (
        report[0] == 0x3F ||
        report[0] == 0x30 ||
        report[0] == 0x21
    ) {
        return BluetoothGamepadProfileV2::NintendoSwitchClassic;
    }

    return descriptorProfile;
}

void BluetoothHidParserV2::applyButton(
    BluetoothGamepadProfileV2 profile,
    std::uint16_t usage,
    std::uint64_t& buttons,
    bool& digitalLeftTrigger,
    bool& digitalRightTrigger
) {
    if (profile == BluetoothGamepadProfileV2::XboxBle) {
        switch (usage) {
            case 1: buttons |= ButtonSouth; break;
            case 2: buttons |= ButtonEast; break;
            case 4: buttons |= ButtonWest; break;
            case 5: buttons |= ButtonNorth; break;
            case 7: buttons |= ButtonLeftBumper; break;
            case 8: buttons |= ButtonRightBumper; break;
            case 11: buttons |= ButtonBack; break;
            case 12: buttons |= ButtonStart; break;
            case 13: buttons |= ButtonGuide; break;
            case 14: buttons |= ButtonLeftStick; break;
            case 15: buttons |= ButtonRightStick; break;
            default: break;
        }
        return;
    }

    if (
        profile == BluetoothGamepadProfileV2::SonyDs4Classic ||
        profile == BluetoothGamepadProfileV2::SonyDualSenseClassic
    ) {
        switch (usage) {
            case 1: buttons |= ButtonWest; break;
            case 2: buttons |= ButtonSouth; break;
            case 3: buttons |= ButtonEast; break;
            case 4: buttons |= ButtonNorth; break;
            case 5: buttons |= ButtonLeftBumper; break;
            case 6: buttons |= ButtonRightBumper; break;
            case 7: digitalLeftTrigger = true; break;
            case 8: digitalRightTrigger = true; break;
            case 9: buttons |= ButtonBack; break;
            case 10: buttons |= ButtonStart; break;
            case 11: buttons |= ButtonLeftStick; break;
            case 12: buttons |= ButtonRightStick; break;
            case 13: buttons |= ButtonGuide; break;
            default: break;
        }
        return;
    }

    switch (usage) {
        case 1: buttons |= ButtonSouth; break;
        case 2: buttons |= ButtonEast; break;
        case 3: buttons |= ButtonWest; break;
        case 4: buttons |= ButtonNorth; break;
        case 5: buttons |= ButtonLeftBumper; break;
        case 6: buttons |= ButtonRightBumper; break;
        case 7: digitalLeftTrigger = true; break;
        case 8: digitalRightTrigger = true; break;
        case 9: buttons |= ButtonBack; break;
        case 10: buttons |= ButtonStart; break;
        case 11: buttons |= ButtonLeftStick; break;
        case 12: buttons |= ButtonRightStick; break;
        case 13: buttons |= ButtonGuide; break;
        default: break;
    }
}

bool BluetoothHidParserV2::parseGamepad(
    DeviceId source,
    TransportType transport,
    const BluetoothHidDescriptorV2& info,
    const std::uint8_t* descriptor,
    std::size_t descriptorLength,
    const std::uint8_t* report,
    std::size_t reportLength,
    std::uint64_t timestampUs,
    UniversalGamepadState& output
) const {
    if (
        !info.hasGamepad ||
        descriptor == nullptr ||
        descriptorLength == 0 ||
        report == nullptr ||
        reportLength == 0 ||
        descriptorLength > std::numeric_limits<std::uint16_t>::max() ||
        reportLength > std::numeric_limits<std::uint16_t>::max()
    ) {
        return false;
    }

    const std::uint16_t reportId =
        incomingReportId(info, report, reportLength);

    const BluetoothGamepadProfileV2 profile =
        effectiveProfile(
            transport,
            info.profile,
            report,
            reportLength
        );

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        static_cast<std::uint16_t>(descriptorLength),
        BT_HID_REPORT_TYPE_INPUT,
        report,
        static_cast<std::uint16_t>(reportLength)
    );

    UniversalGamepadState next =
        output.connected
            ? output
            : UniversalGamepadState {};

    next.source = source;
    next.connected = true;
    next.timestampUs = timestampUs;

    bool sawUseful = false;
    bool sawButtonPage = false;
    std::uint64_t reportButtons = 0;
    bool digitalLeftTrigger = false;
    bool digitalRightTrigger = false;

    bool sawRx = false;
    bool sawRy = false;
    bool sawZ = false;
    bool sawRz = false;

    std::int32_t zValue = 0;
    std::int32_t zMin = 0;
    std::int32_t zMax = 0;
    std::int32_t rzValue = 0;
    std::int32_t rzMin = 0;
    std::int32_t rzMax = 0;

    bool sawAccelerator = false;
    bool sawBrake = false;
    std::int32_t acceleratorValue = 0;
    std::int32_t acceleratorMin = 0;
    std::int32_t acceleratorMax = 0;
    std::int32_t brakeValue = 0;
    std::int32_t brakeMin = 0;
    std::int32_t brakeMax = 0;

    while (btstack_hid_parser_has_more(&parser)) {
        std::uint16_t usagePage = 0;
        std::uint16_t usage = 0;
        std::int32_t value = 0;

        btstack_hid_parser_get_field(
            &parser,
            &usagePage,
            &usage,
            &value
        );

        std::int32_t logicalMin = 0;
        std::int32_t logicalMax = 0;
        findFieldRange(
            info,
            reportId,
            usagePage,
            usage,
            logicalMin,
            logicalMax
        );

        if (usagePage == USAGE_PAGE_BUTTON) {
            sawButtonPage = true;
            sawUseful = true;
            if (value != 0) {
                applyButton(
                    profile,
                    usage,
                    reportButtons,
                    digitalLeftTrigger,
                    digitalRightTrigger
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
                sawUseful = true;
            } else if (usage == USAGE_SIM_BRAKE) {
                sawBrake = true;
                brakeValue = value;
                brakeMin = logicalMin;
                brakeMax = logicalMax;
                sawUseful = true;
            }
            continue;
        }

        if (usagePage != USAGE_PAGE_GENERIC_DESKTOP) {
            continue;
        }

        switch (usage) {
            case USAGE_X:
                next.lx = scaleAxis(value, logicalMin, logicalMax);
                sawUseful = true;
                break;

            case USAGE_Y:
                next.ly = scaleAxis(value, logicalMin, logicalMax);
                sawUseful = true;
                break;

            case USAGE_RX:
                next.rx = scaleAxis(value, logicalMin, logicalMax);
                sawRx = true;
                sawUseful = true;
                break;

            case USAGE_RY:
                next.ry = scaleAxis(value, logicalMin, logicalMax);
                sawRy = true;
                sawUseful = true;
                break;

            case USAGE_Z:
                sawZ = true;
                zValue = value;
                zMin = logicalMin;
                zMax = logicalMax;
                sawUseful = true;
                break;

            case USAGE_RZ:
                sawRz = true;
                rzValue = value;
                rzMin = logicalMin;
                rzMax = logicalMax;
                sawUseful = true;
                break;

            case USAGE_HAT:
                next.dpad =
                    hatToDpad(value, logicalMin, logicalMax);
                sawUseful = true;
                break;

            case USAGE_DPAD_UP:
                if (value != 0) {
                    next.dpad |= static_cast<std::uint8_t>(DpadBits::Up);
                } else {
                    next.dpad &= ~static_cast<std::uint8_t>(DpadBits::Up);
                }
                sawUseful = true;
                break;

            case USAGE_DPAD_DOWN:
                if (value != 0) {
                    next.dpad |= static_cast<std::uint8_t>(DpadBits::Down);
                } else {
                    next.dpad &= ~static_cast<std::uint8_t>(DpadBits::Down);
                }
                sawUseful = true;
                break;

            case USAGE_DPAD_LEFT:
                if (value != 0) {
                    next.dpad |= static_cast<std::uint8_t>(DpadBits::Left);
                } else {
                    next.dpad &= ~static_cast<std::uint8_t>(DpadBits::Left);
                }
                sawUseful = true;
                break;

            case USAGE_DPAD_RIGHT:
                if (value != 0) {
                    next.dpad |= static_cast<std::uint8_t>(DpadBits::Right);
                } else {
                    next.dpad &= ~static_cast<std::uint8_t>(DpadBits::Right);
                }
                sawUseful = true;
                break;

            default:
                break;
        }
    }

    if (!sawUseful) {
        return false;
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
        next.rx = scaleAxis(zValue, zMin, zMax);
        next.ry = scaleAxis(rzValue, rzMin, rzMax);
        sawRx = true;
        sawRy = true;
    }

    if (sawBrake) {
        next.leftTrigger =
            scaleTrigger(brakeValue, brakeMin, brakeMax);
    } else if (sawZ && !sawRx && zMin >= 0) {
        next.leftTrigger =
            scaleTrigger(zValue, zMin, zMax);
    } else if (digitalLeftTrigger) {
        next.leftTrigger =
            std::numeric_limits<std::uint32_t>::max();
    } else if (sawButtonPage) {
        next.leftTrigger = 0;
    }

    if (sawAccelerator) {
        next.rightTrigger =
            scaleTrigger(
                acceleratorValue,
                acceleratorMin,
                acceleratorMax
            );
    } else if (sawRz && !sawRy && rzMin >= 0) {
        next.rightTrigger =
            scaleTrigger(rzValue, rzMin, rzMax);
    } else if (digitalRightTrigger) {
        next.rightTrigger =
            std::numeric_limits<std::uint32_t>::max();
    } else if (sawButtonPage) {
        next.rightTrigger = 0;
    }

    ++next.generation;
    output = next;
    return true;
}

bool BluetoothHidParserV2::parseKeyboard(
    DeviceId source,
    const BluetoothHidDescriptorV2& info,
    const std::uint8_t* descriptor,
    std::size_t descriptorLength,
    const std::uint8_t* report,
    std::size_t reportLength,
    std::uint64_t timestampUs,
    KeyboardState& output
) const {
    if (
        !info.hasKeyboard ||
        descriptor == nullptr ||
        report == nullptr ||
        descriptorLength == 0 ||
        reportLength == 0 ||
        descriptorLength > std::numeric_limits<std::uint16_t>::max() ||
        reportLength > std::numeric_limits<std::uint16_t>::max()
    ) {
        return false;
    }

    const std::uint16_t reportId =
        incomingReportId(info, report, reportLength);

    const BluetoothHidReportMetaV2* meta =
        findReportMeta(info, reportId);

    if (
        meta == nullptr ||
        (meta->flags & kReportKeyboard) == 0
    ) {
        return false;
    }

    KeyboardState next = output;
    next.source = source;
    next.connected = true;
    next.timestampUs = timestampUs;
    next.modifiers = 0;
    next.usages.fill(0);

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        static_cast<std::uint16_t>(descriptorLength),
        BT_HID_REPORT_TYPE_INPUT,
        report,
        static_cast<std::uint16_t>(reportLength)
    );

    while (btstack_hid_parser_has_more(&parser)) {
        std::uint16_t usagePage = 0;
        std::uint16_t usage = 0;
        std::int32_t value = 0;

        btstack_hid_parser_get_field(
            &parser,
            &usagePage,
            &usage,
            &value
        );

        if (
            usagePage != USAGE_PAGE_KEYBOARD ||
            value == 0
        ) {
            continue;
        }

        if (usage >= 0xE0 && usage <= 0xE7) {
            next.modifiers |=
                static_cast<std::uint8_t>(
                    1u << (usage - 0xE0)
                );
        } else if (usage <= 0xFF) {
            next.setPressed(
                static_cast<std::uint8_t>(usage),
                true
            );
        }
    }

    ++next.generation;
    output = next;
    return true;
}

bool BluetoothHidParserV2::parseMouse(
    DeviceId source,
    const BluetoothHidDescriptorV2& info,
    const std::uint8_t* descriptor,
    std::size_t descriptorLength,
    const std::uint8_t* report,
    std::size_t reportLength,
    std::uint64_t timestampUs,
    MouseState& output
) const {
    if (
        !info.hasMouse ||
        descriptor == nullptr ||
        report == nullptr ||
        descriptorLength == 0 ||
        reportLength == 0 ||
        descriptorLength > std::numeric_limits<std::uint16_t>::max() ||
        reportLength > std::numeric_limits<std::uint16_t>::max()
    ) {
        return false;
    }

    const std::uint16_t reportId =
        incomingReportId(info, report, reportLength);

    const BluetoothHidReportMetaV2* meta =
        findReportMeta(info, reportId);

    if (
        meta == nullptr ||
        (meta->flags & (kReportButtons | kReportPointer)) == 0
    ) {
        return false;
    }

    MouseState next = output;
    next.source = source;
    next.connected = true;
    next.timestampUs = timestampUs;
    next.dx = 0;
    next.dy = 0;
    next.wheel = 0;
    next.pan = 0;

    if ((meta->flags & kReportButtons) != 0) {
        next.buttons = 0;
    }

    bool saw = false;

    btstack_hid_parser_t parser {};
    btstack_hid_parser_init(
        &parser,
        descriptor,
        static_cast<std::uint16_t>(descriptorLength),
        BT_HID_REPORT_TYPE_INPUT,
        report,
        static_cast<std::uint16_t>(reportLength)
    );

    while (btstack_hid_parser_has_more(&parser)) {
        std::uint16_t usagePage = 0;
        std::uint16_t usage = 0;
        std::int32_t value = 0;

        btstack_hid_parser_get_field(
            &parser,
            &usagePage,
            &usage,
            &value
        );

        if (
            usagePage == USAGE_PAGE_BUTTON &&
            usage >= 1 &&
            usage <= 5
        ) {
            const std::uint16_t mask =
                static_cast<std::uint16_t>(
                    1u << (usage - 1)
                );

            if (value != 0) {
                next.buttons |= mask;
            } else {
                next.buttons &= static_cast<std::uint16_t>(~mask);
            }

            saw = true;
            continue;
        }

        if (usagePage == USAGE_PAGE_GENERIC_DESKTOP) {
            switch (usage) {
                case USAGE_X:
                    next.dx = value;
                    saw = true;
                    break;

                case USAGE_Y:
                    next.dy = value;
                    saw = true;
                    break;

                case USAGE_WHEEL:
                    next.wheel = static_cast<std::int16_t>(
                        std::clamp<std::int32_t>(
                            value,
                            std::numeric_limits<std::int16_t>::min(),
                            std::numeric_limits<std::int16_t>::max()
                        )
                    );
                    saw = true;
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
            next.pan = static_cast<std::int16_t>(
                std::clamp<std::int32_t>(
                    value,
                    std::numeric_limits<std::int16_t>::min(),
                    std::numeric_limits<std::int16_t>::max()
                )
            );
            saw = true;
        }
    }

    if (!saw) {
        return false;
    }

    ++next.generation;
    output = next;
    return true;
}

} // namespace oag::firmware
