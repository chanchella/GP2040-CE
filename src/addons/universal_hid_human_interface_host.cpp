#include "addons/universal_hid_human_interface_host.h"

#include <cstring>

#include "class/hid/hid.h"
#include "host/usbh.h"
#include "class/hid/hid_host.h"
#include "peripheralmanager.h"
#include "tusb.h"

namespace {

static constexpr uint16_t USAGE_PAGE_GENERIC_DESKTOP = 0x01;
static constexpr uint16_t USAGE_PAGE_KEYBOARD = 0x07;
static constexpr uint16_t USAGE_PAGE_BUTTON = 0x09;
static constexpr uint16_t USAGE_PAGE_CONSUMER = 0x0C;

static constexpr uint16_t USAGE_MOUSE = 0x02;
static constexpr uint16_t USAGE_KEYBOARD = 0x06;
static constexpr uint16_t USAGE_CONSUMER_CONTROL = 0x01;

static constexpr uint16_t USAGE_X = 0x30;
static constexpr uint16_t USAGE_Y = 0x31;
static constexpr uint16_t USAGE_WHEEL = 0x38;
static constexpr uint16_t USAGE_AC_PAN = 0x0238;

struct GlobalState {
    uint16_t usagePage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint8_t reportSize = 0;
    uint16_t reportCount = 0;
    uint8_t reportId = 0;
};

struct LocalState {
    uint16_t usages[
        64
    ] {};
    uint8_t usageCount = 0;

    bool hasUsageMin = false;
    bool hasUsageMax = false;
    uint16_t usageMin = 0;
    uint16_t usageMax = 0;
};

static void clearLocal(LocalState& local) {
    local = LocalState {};
}

static uint32_t readUnsigned(
    uint8_t const* data,
    uint8_t size
) {
    uint32_t value = 0;

    for (uint8_t i = 0; i < size; i++) {
        value |=
            static_cast<uint32_t>(data[i]) <<
            (8u * i);
    }

    return value;
}

static int32_t signExtend(
    uint32_t value,
    uint8_t size
) {
    if (size == 0 || size >= 4) {
        return static_cast<int32_t>(value);
    }

    const uint8_t bits =
        static_cast<uint8_t>(size * 8u);
    const uint32_t sign =
        1u << (bits - 1u);

    if ((value & sign) != 0) {
        value |= ~((1u << bits) - 1u);
    }

    return static_cast<int32_t>(value);
}

static uint16_t localUsage(
    LocalState const& local,
    uint16_t index
) {
    if (index < local.usageCount) {
        return local.usages[index];
    }

    if (local.hasUsageMin && local.hasUsageMax) {
        const uint32_t usage =
            static_cast<uint32_t>(local.usageMin) +
            index;

        if (usage <= local.usageMax) {
            return static_cast<uint16_t>(usage);
        }
    }

    if (local.usageCount != 0) {
        return local.usages[
            local.usageCount - 1
        ];
    }

    return 0;
}

} // namespace

bool UniversalHIDHumanInterfaceHostAddon::available() {
    return
        UNIVERSAL_HID_HUMAN_INTERFACE_HOST_ENABLED &&
        PeripheralManager::getInstance().isUSBEnabled(0);
}

void UniversalHIDHumanInterfaceHostAddon::setup() {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        resetInterface(interfaces[i]);
    }
}

void UniversalHIDHumanInterfaceHostAddon::preprocess() {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState& state = interfaces[i];

        if (!state.active) {
            continue;
        }

        if (state.descriptorPending) {
            state.descriptorPending = false;
            parseDescriptor(state);
        }

        if (state.reportPending) {
            state.reportPending = false;
            processReport(state);
        }
    }

    serviceKeyboardLeds();
}

void UniversalHIDHumanInterfaceHostAddon::resetInterface(
    InterfaceState& state
) {
    state = InterfaceState {};
}

UniversalHIDHumanInterfaceHostAddon::InterfaceState*
UniversalHIDHumanInterfaceHostAddon::findInterface(
    uint8_t devAddr,
    uint8_t instance
) {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState& state = interfaces[i];

        if (
            state.active &&
            state.devAddr == devAddr &&
            state.instance == instance
        ) {
            return &state;
        }
    }

    return nullptr;
}

UniversalHIDHumanInterfaceHostAddon::InterfaceState*
UniversalHIDHumanInterfaceHostAddon::allocateInterface() {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        if (!interfaces[i].active) {
            return &interfaces[i];
        }
    }

    return nullptr;
}

void UniversalHIDHumanInterfaceHostAddon::mount(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* desc_report,
    uint16_t desc_len
) {
    InterfaceState* state =
        findInterface(dev_addr, instance);

    if (state == nullptr) {
        state = allocateInterface();
    }

    if (state == nullptr) {
        return;
    }

    resetInterface(*state);

    state->active = true;
    state->devAddr = dev_addr;
    state->instance = instance;
    state->protocol =
        tuh_hid_interface_protocol(
            dev_addr,
            instance
        );

    tuh_vid_pid_get(
        dev_addr,
        &state->vid,
        &state->pid
    );

    if (desc_report != nullptr && desc_len != 0) {
        state->descriptorLength =
            desc_len > MAX_DESCRIPTOR_BYTES
                ? MAX_DESCRIPTOR_BYTES
                : desc_len;

        std::memcpy(
            state->descriptor,
            desc_report,
            state->descriptorLength
        );

        state->descriptorPending = true;
        return;
    }

    // Boot-protocol fallback for devices that do not expose a usable
    // report descriptor through the mount callback.
    state->parsed = true;
    state->isKeyboard =
        state->protocol ==
        HID_ITF_PROTOCOL_KEYBOARD;
    state->isMouse =
        state->protocol ==
        HID_ITF_PROTOCOL_MOUSE;
}

void UniversalHIDHumanInterfaceHostAddon::unmount(
    uint8_t dev_addr
) {
    UHIDINPUT.releaseUsbKeyboardDevice(
        UniversalHumanInterfaceSource::USB_HID,
        dev_addr
    );

    UHIDINPUT.releaseUsbMouseDevice(
        UniversalHumanInterfaceSource::USB_HID,
        dev_addr
    );

    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        if (
            interfaces[i].active &&
            interfaces[i].devAddr == dev_addr
        ) {
            resetInterface(interfaces[i]);
        }
    }
}

void UniversalHIDHumanInterfaceHostAddon::serviceKeyboardLeds() {
    uint8_t desired = 0;
    uint32_t generation = 0;
    UHIDINPUT.snapshotKeyboardLedState(desired, generation);
    (void)generation;

    // Start at most one control transfer per loop. This avoids overlapping
    // SET_REPORT requests on composite devices that share endpoint zero.
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState& state = interfaces[i];

        if (
            !state.active ||
            state.ledTransferPending ||
            state.appliedLedState == desired ||
            !(state.isKeyboard ||
              state.protocol == HID_ITF_PROTOCOL_KEYBOARD)
        ) {
            continue;
        }

        state.ledReportValue = desired;

        if (tuh_hid_set_report(
                state.devAddr,
                state.instance,
                0,
                HID_REPORT_TYPE_OUTPUT,
                &state.ledReportValue,
                1
            )) {
            state.ledTransferPending = true;
        } else {
            // One safe attempt per LED state. A non-standard gaming keyboard
            // may use vendor reports for RGB/backlight; do not hammer EP0.
            state.appliedLedState = desired;
        }

        break;
    }
}

void UniversalHIDHumanInterfaceHostAddon::set_report_complete(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t report_id,
    uint8_t report_type,
    uint16_t len
) {
    (void)report_id;
    (void)len;

    if (report_type != HID_REPORT_TYPE_OUTPUT) {
        return;
    }

    InterfaceState* state = findInterface(dev_addr, instance);
    if (state == nullptr || !state->ledTransferPending) {
        return;
    }

    // Mark this state attempted whether the downstream keyboard accepted or
    // stalled it. A later Caps/Num/Scroll change will trigger a new attempt.
    state->appliedLedState = state->ledReportValue;
    state->ledTransferPending = false;
}

void UniversalHIDHumanInterfaceHostAddon::report_received(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* report,
    uint16_t len
) {
    InterfaceState* state =
        findInterface(dev_addr, instance);

    if (
        state == nullptr ||
        report == nullptr ||
        len == 0
    ) {
        return;
    }

    state->reportLength =
        len > MAX_REPORT_BYTES
            ? MAX_REPORT_BYTES
            : static_cast<uint8_t>(len);

    std::memcpy(
        state->report,
        report,
        state->reportLength
    );

    state->reportPending = true;
}

UniversalHIDHumanInterfaceHostAddon::
KeyboardReportContribution*
UniversalHIDHumanInterfaceHostAddon::
findOrCreateKeyboardContribution(
    InterfaceState& state,
    uint8_t reportId
) {
    for (
        uint8_t i = 0;
        i < MAX_REPORT_CONTRIBUTIONS;
        i++
    ) {
        KeyboardReportContribution& item =
            state.keyboardReports[i];

        if (
            item.used &&
            item.reportId == reportId
        ) {
            return &item;
        }
    }

    for (
        uint8_t i = 0;
        i < MAX_REPORT_CONTRIBUTIONS;
        i++
    ) {
        KeyboardReportContribution& item =
            state.keyboardReports[i];

        if (!item.used) {
            item = KeyboardReportContribution {};
            item.used = true;
            item.reportId = reportId;
            return &item;
        }
    }

    return nullptr;
}

UniversalHIDHumanInterfaceHostAddon::
MouseReportContribution*
UniversalHIDHumanInterfaceHostAddon::
findOrCreateMouseContribution(
    InterfaceState& state,
    uint8_t reportId
) {
    for (
        uint8_t i = 0;
        i < MAX_REPORT_CONTRIBUTIONS;
        i++
    ) {
        MouseReportContribution& item =
            state.mouseReports[i];

        if (
            item.used &&
            item.reportId == reportId
        ) {
            return &item;
        }
    }

    for (
        uint8_t i = 0;
        i < MAX_REPORT_CONTRIBUTIONS;
        i++
    ) {
        MouseReportContribution& item =
            state.mouseReports[i];

        if (!item.used) {
            item = MouseReportContribution {};
            item.used = true;
            item.reportId = reportId;
            return &item;
        }
    }

    return nullptr;
}

void UniversalHIDHumanInterfaceHostAddon::
ensureKeyboardSlot(
    InterfaceState& state
) {
    if (
        state.keyboardSlot !=
        UNIVERSAL_HID_SLOT_INVALID
    ) {
        return;
    }

    // instance=0 intentionally coalesces keyboard + consumer-control
    // interfaces belonging to the same physical USB device.
    state.keyboardSlot =
        UHIDINPUT.claimUsbKeyboardSlot(
            UniversalHumanInterfaceSource::USB_HID,
            UniversalTransport::USB_WIRED,
            state.vid,
            state.pid,
            state.devAddr,
            0
        );
}

void UniversalHIDHumanInterfaceHostAddon::
ensureMouseSlot(
    InterfaceState& state
) {
    if (
        state.mouseSlot !=
        UNIVERSAL_HID_SLOT_INVALID
    ) {
        return;
    }

    state.mouseSlot =
        UHIDINPUT.claimUsbMouseSlot(
            UniversalHumanInterfaceSource::USB_HID,
            UniversalTransport::USB_WIRED,
            state.vid,
            state.pid,
            state.devAddr,
            0
        );
}

void UniversalHIDHumanInterfaceHostAddon::
appendConsumerUsage(
    UniversalKeyboardState& state,
    uint16_t usage
) {
    if (usage == 0) {
        return;
    }

    for (
        uint8_t i = 0;
        i < state.consumerUsageCount;
        i++
    ) {
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

    state.consumerUsages[
        state.consumerUsageCount++
    ] = usage;
}

void UniversalHIDHumanInterfaceHostAddon::
publishKeyboardAggregate(
    uint8_t devAddr,
    uint8_t slot
) {
    if (slot == UNIVERSAL_HID_SLOT_INVALID) {
        return;
    }

    UniversalKeyboardState aggregate {};

    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState const& state = interfaces[i];

        if (
            !state.active ||
            state.devAddr != devAddr ||
            state.keyboardSlot != slot
        ) {
            continue;
        }

        for (
            uint8_t r = 0;
            r < MAX_REPORT_CONTRIBUTIONS;
            r++
        ) {
            KeyboardReportContribution const& contribution =
                state.keyboardReports[r];

            if (!contribution.used) {
                continue;
            }

            for (
                uint8_t word = 0;
                word <
                    UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS;
                word++
            ) {
                aggregate.keys[word] |=
                    contribution.state.keys[word];
            }

            aggregate.modifiers |=
                contribution.state.modifiers;

            for (
                uint8_t c = 0;
                c <
                    contribution.state.consumerUsageCount;
                c++
            ) {
                appendConsumerUsage(
                    aggregate,
                    contribution.state.consumerUsages[c]
                );
            }
        }
    }

    UHIDINPUT.publishKeyboard(
        slot,
        aggregate
    );
}

uint32_t UniversalHIDHumanInterfaceHostAddon::
aggregateMouseButtons(
    uint8_t devAddr,
    uint8_t slot
) const {
    uint32_t buttons = 0;

    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState const& state = interfaces[i];

        if (
            !state.active ||
            state.devAddr != devAddr ||
            state.mouseSlot != slot
        ) {
            continue;
        }

        for (
            uint8_t r = 0;
            r < MAX_REPORT_CONTRIBUTIONS;
            r++
        ) {
            if (state.mouseReports[r].used) {
                buttons |=
                    state.mouseReports[r].buttons;
            }
        }
    }

    return buttons;
}

void UniversalHIDHumanInterfaceHostAddon::
parseDescriptor(
    InterfaceState& state
) {
    state.parsed = true;
    state.isKeyboard = false;
    state.isMouse = false;
    state.hasConsumer = false;
    state.usesReportIds = false;
    state.fieldCount = 0;

    GlobalState global {};
    GlobalState globalStack[GLOBAL_STACK_DEPTH] {};
    uint8_t globalStackCount = 0;

    LocalState local {};
    uint16_t reportBitOffsets[256] {};

    uint8_t collectionDepth = 0;
    uint8_t keyboardCollectionDepth = 0;
    uint8_t mouseCollectionDepth = 0;
    uint8_t consumerCollectionDepth = 0;

    uint16_t offset = 0;

    while (offset < state.descriptorLength) {
        const uint8_t prefix =
            state.descriptor[offset++];

        if (prefix == 0xFE) {
            if (
                offset + 2 >
                state.descriptorLength
            ) {
                break;
            }

            const uint8_t size =
                state.descriptor[offset];

            offset =
                static_cast<uint16_t>(
                    offset + 2
                );

            if (
                offset + size >
                state.descriptorLength
            ) {
                break;
            }

            offset =
                static_cast<uint16_t>(
                    offset + size
                );
            continue;
        }

        const uint8_t sizeCode =
            prefix & 0x03;
        const uint8_t dataSize =
            sizeCode == 3 ? 4 : sizeCode;
        const uint8_t type =
            (prefix >> 2) & 0x03;
        const uint8_t tag =
            (prefix >> 4) & 0x0F;

        if (
            offset + dataSize >
            state.descriptorLength
        ) {
            break;
        }

        uint8_t const* data =
            &state.descriptor[offset];

        const uint32_t unsignedValue =
            readUnsigned(
                data,
                dataSize
            );

        const int32_t signedValue =
            signExtend(
                unsignedValue,
                dataSize
            );

        offset =
            static_cast<uint16_t>(
                offset + dataSize
            );

        if (type == 0) {
            // Input Main item.
            if (tag == 8) {
                uint16_t& bitOffset =
                    reportBitOffsets[
                        global.reportId
                    ];

                const bool isConstant =
                    (unsignedValue & 0x01u) != 0;
                const bool isVariable =
                    (unsignedValue & 0x02u) != 0;

                FieldTarget target =
                    FieldTarget::NONE;

                if (mouseCollectionDepth != 0) {
                    target = FieldTarget::MOUSE;
                } else if (
                    keyboardCollectionDepth != 0
                ) {
                    target = FieldTarget::KEYBOARD;
                } else if (
                    consumerCollectionDepth != 0
                ) {
                    target = FieldTarget::CONSUMER;
                }

                if (
                    target != FieldTarget::NONE &&
                    !isConstant &&
                    global.reportSize != 0 &&
                    global.reportSize <= 32
                ) {
                    for (
                        uint16_t i = 0;
                        i < global.reportCount;
                        i++
                    ) {
                        if (state.fieldCount < MAX_FIELDS) {
                            HidField& field =
                                state.fields[
                                    state.fieldCount++
                                ];

                            field.used = true;
                            field.isArray = !isVariable;
                            field.target = target;
                            field.reportId = global.reportId;
                            field.bitOffset = bitOffset;
                            field.bitSize =
                                global.reportSize;
                            field.usagePage =
                                global.usagePage;
                            field.usage =
                                isVariable
                                    ? localUsage(local, i)
                                    : 0;
                            field.usageMin =
                                local.hasUsageMin
                                    ? local.usageMin
                                    : 0;
                            field.usageMax =
                                local.hasUsageMax
                                    ? local.usageMax
                                    : 0;
                            field.logicalMin =
                                global.logicalMin;
                            field.logicalMax =
                                global.logicalMax;
                        }

                        bitOffset =
                            static_cast<uint16_t>(
                                bitOffset +
                                global.reportSize
                            );
                    }
                } else {
                    bitOffset =
                        static_cast<uint16_t>(
                            bitOffset +
                            static_cast<uint32_t>(
                                global.reportSize
                            ) *
                            global.reportCount
                        );
                }

                clearLocal(local);
                continue;
            }

            // Collection Main item.
            if (tag == 10) {
                const uint16_t usage =
                    localUsage(local, 0);

                const uint8_t newDepth =
                    static_cast<uint8_t>(
                        collectionDepth + 1
                    );

                if (collectionDepth == 0) {
                    if (
                        global.usagePage ==
                            USAGE_PAGE_GENERIC_DESKTOP &&
                        usage == USAGE_KEYBOARD
                    ) {
                        state.isKeyboard = true;
                        keyboardCollectionDepth = newDepth;
                    } else if (
                        global.usagePage ==
                            USAGE_PAGE_GENERIC_DESKTOP &&
                        usage == USAGE_MOUSE
                    ) {
                        state.isMouse = true;
                        mouseCollectionDepth = newDepth;
                    } else if (
                        global.usagePage ==
                            USAGE_PAGE_CONSUMER &&
                        usage ==
                            USAGE_CONSUMER_CONTROL
                    ) {
                        state.hasConsumer = true;
                        consumerCollectionDepth = newDepth;
                    }
                }

                collectionDepth = newDepth;
                clearLocal(local);
                continue;
            }

            // End Collection.
            if (tag == 12) {
                if (
                    collectionDepth ==
                    keyboardCollectionDepth
                ) {
                    keyboardCollectionDepth = 0;
                }

                if (
                    collectionDepth ==
                    mouseCollectionDepth
                ) {
                    mouseCollectionDepth = 0;
                }

                if (
                    collectionDepth ==
                    consumerCollectionDepth
                ) {
                    consumerCollectionDepth = 0;
                }

                if (collectionDepth > 0) {
                    collectionDepth--;
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
                        static_cast<uint16_t>(
                            unsignedValue
                        );
                    break;

                case 1:
                    global.logicalMin = signedValue;
                    break;

                case 2:
                    global.logicalMax =
                        global.logicalMin < 0
                            ? signedValue
                            : static_cast<int32_t>(
                                unsignedValue
                            );
                    break;

                case 7:
                    global.reportSize =
                        static_cast<uint8_t>(
                            unsignedValue
                        );
                    break;

                case 8:
                    global.reportId =
                        static_cast<uint8_t>(
                            unsignedValue
                        );
                    state.usesReportIds = true;
                    break;

                case 9:
                    global.reportCount =
                        static_cast<uint16_t>(
                            unsignedValue
                        );
                    break;

                case 10:
                    if (
                        globalStackCount <
                        GLOBAL_STACK_DEPTH
                    ) {
                        globalStack[
                            globalStackCount++
                        ] = global;
                    }
                    break;

                case 11:
                    if (globalStackCount > 0) {
                        global =
                            globalStack[
                                --globalStackCount
                            ];
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
                    if (
                        local.usageCount <
                        MAX_LOCAL_USAGES
                    ) {
                        local.usages[
                            local.usageCount++
                        ] =
                            static_cast<uint16_t>(
                                unsignedValue
                            );
                    }
                    break;

                case 1:
                    local.hasUsageMin = true;
                    local.usageMin =
                        static_cast<uint16_t>(
                            unsignedValue
                        );
                    break;

                case 2:
                    local.hasUsageMax = true;
                    local.usageMax =
                        static_cast<uint16_t>(
                            unsignedValue
                        );
                    break;

                default:
                    break;
            }
        }
    }

    // Boot protocol is a safe fallback if the descriptor is odd but the
    // interface explicitly identifies itself as keyboard or mouse.
    if (
        !state.isKeyboard &&
        state.protocol ==
            HID_ITF_PROTOCOL_KEYBOARD
    ) {
        state.isKeyboard = true;
    }

    if (
        !state.isMouse &&
        state.protocol ==
            HID_ITF_PROTOCOL_MOUSE
    ) {
        state.isMouse = true;
    }
}

uint32_t UniversalHIDHumanInterfaceHostAddon::
extractBits(
    uint8_t const* data,
    uint16_t dataLength,
    uint16_t bitOffset,
    uint8_t bitSize
) {
    if (
        data == nullptr ||
        bitSize == 0 ||
        bitSize > 32
    ) {
        return 0;
    }

    uint32_t value = 0;

    for (
        uint8_t bit = 0;
        bit < bitSize;
        bit++
    ) {
        const uint32_t absoluteBit =
            static_cast<uint32_t>(
                bitOffset
            ) + bit;

        const uint16_t byteIndex =
            static_cast<uint16_t>(
                absoluteBit / 8u
            );

        if (byteIndex >= dataLength) {
            break;
        }

        const uint8_t bitIndex =
            static_cast<uint8_t>(
                absoluteBit % 8u
            );

        if (
            data[byteIndex] &
            (1u << bitIndex)
        ) {
            value |= 1u << bit;
        }
    }

    return value;
}

int32_t UniversalHIDHumanInterfaceHostAddon::
signedFieldValue(
    HidField const& field,
    uint32_t raw
) {
    if (
        field.logicalMin >= 0 ||
        field.bitSize >= 32
    ) {
        return static_cast<int32_t>(raw);
    }

    const uint32_t sign =
        1u << (field.bitSize - 1u);

    if ((raw & sign) != 0) {
        raw |=
            ~((1u << field.bitSize) - 1u);
    }

    return static_cast<int32_t>(raw);
}

void UniversalHIDHumanInterfaceHostAddon::
processReport(
    InterfaceState& state
) {
    if (
        !state.parsed ||
        state.reportLength == 0
    ) {
        return;
    }

    uint8_t incomingReportId = 0;
    uint8_t const* payload = state.report;
    uint16_t payloadLength =
        state.reportLength;

    if (state.usesReportIds) {
        incomingReportId = state.report[0];

        if (payloadLength <= 1) {
            return;
        }

        payload = &state.report[1];
        payloadLength--;
    }

    bool reportHasKeyboardFields = false;
    bool reportHasConsumerFields = false;
    bool reportHasMouseButtons = false;

    for (
        uint16_t i = 0;
        i < state.fieldCount;
        i++
    ) {
        HidField const& field =
            state.fields[i];

        if (
            !field.used ||
            field.reportId != incomingReportId
        ) {
            continue;
        }

        if (
            field.target ==
                FieldTarget::KEYBOARD &&
            field.usagePage ==
                USAGE_PAGE_KEYBOARD
        ) {
            reportHasKeyboardFields = true;
        }

        if (
            (
                field.target ==
                    FieldTarget::KEYBOARD ||
                field.target ==
                    FieldTarget::CONSUMER
            ) &&
            field.usagePage ==
                USAGE_PAGE_CONSUMER &&
            field.usage != USAGE_AC_PAN
        ) {
            reportHasConsumerFields = true;
        }

        if (
            field.target ==
                FieldTarget::MOUSE &&
            field.usagePage ==
                USAGE_PAGE_BUTTON
        ) {
            reportHasMouseButtons = true;
        }
    }

    KeyboardReportContribution* keyboard =
        nullptr;

    if (
        reportHasKeyboardFields ||
        reportHasConsumerFields ||
        state.isKeyboard ||
        state.hasConsumer
    ) {
        keyboard =
            findOrCreateKeyboardContribution(
                state,
                incomingReportId
            );
    }

    if (
        keyboard != nullptr &&
        reportHasKeyboardFields
    ) {
        for (
            uint8_t word = 0;
            word <
                UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS;
            word++
        ) {
            keyboard->state.keys[word] = 0;
        }

        keyboard->state.modifiers = 0;
    }

    if (
        keyboard != nullptr &&
        reportHasConsumerFields
    ) {
        keyboard->state.consumerUsageCount = 0;
        std::memset(
            keyboard->state.consumerUsages,
            0,
            sizeof(
                keyboard->state.consumerUsages
            )
        );
    }

    MouseReportContribution* mouse =
        nullptr;

    if (state.isMouse || reportHasMouseButtons) {
        mouse =
            findOrCreateMouseContribution(
                state,
                incomingReportId
            );
    }

    if (
        mouse != nullptr &&
        reportHasMouseButtons
    ) {
        mouse->buttons = 0;
    }

    UniversalMouseState mouseReport {};
    bool sawKeyboard = false;
    bool sawConsumer = false;
    bool sawMouse = false;

    for (
        uint16_t i = 0;
        i < state.fieldCount;
        i++
    ) {
        HidField const& field =
            state.fields[i];

        if (
            !field.used ||
            field.reportId != incomingReportId
        ) {
            continue;
        }

        const uint32_t raw =
            extractBits(
                payload,
                payloadLength,
                field.bitOffset,
                field.bitSize
            );

        const int32_t value =
            signedFieldValue(
                field,
                raw
            );

        if (
            field.target ==
                FieldTarget::KEYBOARD &&
            field.usagePage ==
                USAGE_PAGE_KEYBOARD &&
            keyboard != nullptr
        ) {
            const uint16_t keyUsage =
                field.isArray
                    ? static_cast<uint16_t>(raw)
                    : field.usage;

            const bool pressed =
                field.isArray
                    ? raw != 0
                    : value != 0;

            if (
                pressed &&
                keyUsage >= 0xE0 &&
                keyUsage <= 0xE7
            ) {
                keyboard->state.modifiers |=
                    static_cast<uint8_t>(
                        1u <<
                        (keyUsage - 0xE0)
                    );
            } else if (
                pressed &&
                keyUsage <= 0xFF
            ) {
                keyboard->state.setKeyDown(
                    static_cast<uint8_t>(
                        keyUsage
                    ),
                    true
                );
            }

            sawKeyboard = true;
            continue;
        }

        if (
            (
                field.target ==
                    FieldTarget::KEYBOARD ||
                field.target ==
                    FieldTarget::CONSUMER
            ) &&
            field.usagePage ==
                USAGE_PAGE_CONSUMER
        ) {
            const uint16_t consumerUsage =
                field.isArray
                    ? static_cast<uint16_t>(raw)
                    : field.usage;

            if (
                consumerUsage ==
                    USAGE_AC_PAN
            ) {
                mouseReport.horizontalWheel =
                    value;
                sawMouse = true;
            } else if (
                keyboard != nullptr &&
                (
                    field.isArray
                        ? raw != 0
                        : value != 0
                )
            ) {
                appendConsumerUsage(
                    keyboard->state,
                    consumerUsage
                );
                sawConsumer = true;
            }

            continue;
        }

        if (
            field.target !=
            FieldTarget::MOUSE
        ) {
            continue;
        }

        if (
            field.usagePage ==
                USAGE_PAGE_BUTTON &&
            field.usage >= 1 &&
            field.usage <= 32 &&
            mouse != nullptr
        ) {
            const uint32_t mask =
                1u << (field.usage - 1);

            if (value != 0) {
                mouse->buttons |= mask;
            } else {
                mouse->buttons &= ~mask;
            }

            sawMouse = true;
            continue;
        }

        if (
            field.usagePage ==
            USAGE_PAGE_GENERIC_DESKTOP
        ) {
            switch (field.usage) {
                case USAGE_X:
                    mouseReport.x = value;
                    sawMouse = true;
                    break;

                case USAGE_Y:
                    mouseReport.y = value;
                    sawMouse = true;
                    break;

                case USAGE_WHEEL:
                    mouseReport.wheel = value;
                    sawMouse = true;
                    break;

                default:
                    break;
            }
        }

        if (
            field.usagePage ==
                USAGE_PAGE_CONSUMER &&
            field.usage ==
                USAGE_AC_PAN
        ) {
            mouseReport.horizontalWheel =
                value;
            sawMouse = true;
        }
    }

    // Descriptor-less boot fallback.
    if (
        state.fieldCount == 0 &&
        state.protocol ==
            HID_ITF_PROTOCOL_KEYBOARD &&
        state.reportLength >= 8
    ) {
        keyboard =
            findOrCreateKeyboardContribution(
                state,
                0
            );

        if (keyboard != nullptr) {
            keyboard->state =
                UniversalKeyboardState {};

            keyboard->state.modifiers =
                state.report[0];

            for (
                uint8_t i = 2;
                i < 8;
                i++
            ) {
                const uint8_t usage =
                    state.report[i];

                if (usage != 0) {
                    keyboard->state.setKeyDown(
                        usage,
                        true
                    );
                }
            }

            sawKeyboard = true;
        }
    }

    if (
        state.fieldCount == 0 &&
        state.protocol ==
            HID_ITF_PROTOCOL_MOUSE &&
        state.reportLength >= 4
    ) {
        mouse =
            findOrCreateMouseContribution(
                state,
                0
            );

        if (mouse != nullptr) {
            mouse->buttons =
                state.report[0];

            mouseReport.x =
                static_cast<int8_t>(
                    state.report[1]
                );

            mouseReport.y =
                static_cast<int8_t>(
                    state.report[2]
                );

            mouseReport.wheel =
                static_cast<int8_t>(
                    state.report[3]
                );

            if (state.reportLength >= 5) {
                mouseReport.horizontalWheel =
                    static_cast<int8_t>(
                        state.report[4]
                    );
            }

            sawMouse = true;
        }
    }

    if (
        keyboard != nullptr &&
        (
            reportHasKeyboardFields ||
            reportHasConsumerFields ||
            sawKeyboard ||
            sawConsumer
        )
    ) {
        ensureKeyboardSlot(state);

        publishKeyboardAggregate(
            state.devAddr,
            state.keyboardSlot
        );
    }

    if (sawMouse) {
        ensureMouseSlot(state);

        if (
            state.mouseSlot !=
            UNIVERSAL_HID_SLOT_INVALID
        ) {
            mouseReport.buttons =
                aggregateMouseButtons(
                    state.devAddr,
                    state.mouseSlot
                );

            UHIDINPUT.publishMouse(
                state.mouseSlot,
                mouseReport
            );
        }
    }
}
