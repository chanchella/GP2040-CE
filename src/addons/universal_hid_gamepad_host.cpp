#include "addons/universal_hid_gamepad_host.h"

#include <cstring>

#include "class/hid/hid.h"
#include "class/hid/hid_host.h"
#include "peripheralmanager.h"
#include "tusb.h"

namespace {

static constexpr uint16_t USAGE_PAGE_GENERIC_DESKTOP = 0x01;
static constexpr uint16_t USAGE_PAGE_BUTTON = 0x09;

static constexpr uint16_t USAGE_JOYSTICK = 0x04;
static constexpr uint16_t USAGE_GAMEPAD = 0x05;
static constexpr uint16_t USAGE_MULTI_AXIS = 0x08;

static constexpr uint16_t USAGE_X = 0x30;
static constexpr uint16_t USAGE_Y = 0x31;
static constexpr uint16_t USAGE_Z = 0x32;
static constexpr uint16_t USAGE_RX = 0x33;
static constexpr uint16_t USAGE_RY = 0x34;
static constexpr uint16_t USAGE_RZ = 0x35;
static constexpr uint16_t USAGE_HAT = 0x39;

struct GlobalState {
    uint16_t usagePage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint8_t reportSize = 0;
    uint8_t reportCount = 0;
    uint8_t reportId = 0;
};

struct LocalState {
    uint16_t usages[32] {};
    uint8_t usageCount = 0;
    bool hasUsageMin = false;
    bool hasUsageMax = false;
    uint16_t usageMin = 0;
    uint16_t usageMax = 0;
};

static void clearLocal(LocalState& local) {
    local = LocalState {};
}

static uint32_t readUnsigned(uint8_t const* data, uint8_t size) {
    uint32_t value = 0;

    for (uint8_t i = 0; i < size; i++) {
        value |= static_cast<uint32_t>(data[i]) << (8u * i);
    }

    return value;
}

static int32_t signExtend(uint32_t value, uint8_t size) {
    if (size == 0 || size >= 4) {
        return static_cast<int32_t>(value);
    }

    const uint8_t bits = static_cast<uint8_t>(size * 8u);
    const uint32_t sign = 1u << (bits - 1u);

    if (value & sign) {
        value |= ~((1u << bits) - 1u);
    }

    return static_cast<int32_t>(value);
}

static uint16_t localUsage(LocalState const& local, uint8_t index) {
    if (index < local.usageCount) {
        return local.usages[index];
    }

    if (local.hasUsageMin && local.hasUsageMax) {
        const uint16_t usage =
            static_cast<uint16_t>(local.usageMin + index);

        if (usage <= local.usageMax) {
            return usage;
        }
    }

    if (local.usageCount != 0) {
        return local.usages[local.usageCount - 1];
    }

    return 0;
}

static bool gamepadUsage(uint16_t page, uint16_t usage) {
    return
        page == USAGE_PAGE_GENERIC_DESKTOP &&
        (
            usage == USAGE_JOYSTICK ||
            usage == USAGE_GAMEPAD ||
            usage == USAGE_MULTI_AXIS
        );
}

static int32_t signedFieldValue(
    UniversalHIDGamepadHostAddon::HidField const& field,
    uint32_t raw
) {
    if (field.logicalMin >= 0 || field.bitSize >= 32) {
        return static_cast<int32_t>(raw);
    }

    const uint32_t sign = 1u << (field.bitSize - 1u);

    if (raw & sign) {
        raw |= ~((1u << field.bitSize) - 1u);
    }

    return static_cast<int32_t>(raw);
}

} // namespace

bool UniversalHIDGamepadHostAddon::available() {
    return UNIVERSAL_HID_GAMEPAD_HOST_ENABLED &&
           PeripheralManager::getInstance().isUSBEnabled(0);
}

void UniversalHIDGamepadHostAddon::setup() {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        resetInterface(interfaces[i]);
    }
}

void UniversalHIDGamepadHostAddon::preprocess() {
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
}

void UniversalHIDGamepadHostAddon::resetInterface(
    InterfaceState& state
) {
    state = InterfaceState {};
}

UniversalHIDGamepadHostAddon::InterfaceState*
UniversalHIDGamepadHostAddon::findInterface(
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

UniversalHIDGamepadHostAddon::InterfaceState*
UniversalHIDGamepadHostAddon::allocateInterface() {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        if (!interfaces[i].active) {
            return &interfaces[i];
        }
    }

    return nullptr;
}

void UniversalHIDGamepadHostAddon::mount(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const* desc_report,
    uint16_t desc_len
) {
    const uint8_t protocol =
        tuh_hid_interface_protocol(dev_addr, instance);

    // Keyboard and mouse remain in their dedicated host paths.
    if (
        protocol == HID_ITF_PROTOCOL_KEYBOARD ||
        protocol == HID_ITF_PROTOCOL_MOUSE ||
        desc_report == nullptr ||
        desc_len == 0
    ) {
        return;
    }

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
    state->protocol = protocol;

    tuh_vid_pid_get(
        dev_addr,
        &state->vid,
        &state->pid
    );

    state->descriptorLength =
        desc_len > MAX_DESCRIPTOR_BYTES
            ? MAX_DESCRIPTOR_BYTES
            : desc_len;

    memcpy(
        state->descriptor,
        desc_report,
        state->descriptorLength
    );

    state->descriptorPending = true;
}

void UniversalHIDGamepadHostAddon::unmount(uint8_t dev_addr) {
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceState& state = interfaces[i];

        if (!state.active || state.devAddr != dev_addr) {
            continue;
        }

        if (state.globalSlot != UNIVERSAL_INPUT_SLOT_INVALID) {
            UINPUT.disconnect(state.globalSlot);
        }

        resetInterface(state);
    }
}

void UniversalHIDGamepadHostAddon::report_received(
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

    memcpy(
        state->report,
        report,
        state->reportLength
    );

    state->reportPending = true;
}

void UniversalHIDGamepadHostAddon::parseDescriptor(
    InterfaceState& state
) {
    state.parsed = true;
    state.isGamepad = false;
    state.fieldCount = 0;
    state.topUsagePage = 0;
    state.topUsage = 0;
    state.lastStateValid = false;

    GlobalState global {};
    GlobalState globalStack[GLOBAL_STACK_DEPTH] {};
    uint8_t globalStackCount = 0;

    LocalState local {};
    uint16_t reportBitOffsets[256] {};

    uint8_t collectionDepth = 0;
    uint8_t gamepadCollectionDepth = 0;

    uint16_t offset = 0;

    while (offset < state.descriptorLength) {
        const uint8_t prefix = state.descriptor[offset++];

        // HID long item.
        if (prefix == 0xFE) {
            if (offset + 2 > state.descriptorLength) {
                break;
            }

            const uint8_t size = state.descriptor[offset];
            offset = static_cast<uint16_t>(offset + 2);

            if (offset + size > state.descriptorLength) {
                break;
            }

            offset = static_cast<uint16_t>(offset + size);
            continue;
        }

        const uint8_t sizeCode = prefix & 0x03;
        const uint8_t dataSize =
            sizeCode == 3 ? 4 : sizeCode;
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = (prefix >> 4) & 0x0F;

        if (offset + dataSize > state.descriptorLength) {
            break;
        }

        uint8_t const* data = &state.descriptor[offset];
        const uint32_t unsignedValue =
            readUnsigned(data, dataSize);
        const int32_t signedValue =
            signExtend(unsignedValue, dataSize);

        offset = static_cast<uint16_t>(offset + dataSize);

        // Main items.
        if (type == 0) {
            // INPUT
            if (tag == 8) {
                uint16_t& bitOffset =
                    reportBitOffsets[global.reportId];

                const bool isConstant =
                    (unsignedValue & 0x01u) != 0;
                const bool isVariable =
                    (unsignedValue & 0x02u) != 0;

                if (
                    gamepadCollectionDepth != 0 &&
                    !isConstant &&
                    isVariable
                ) {
                    for (
                        uint8_t i = 0;
                        i < global.reportCount;
                        i++
                    ) {
                        if (
                            state.fieldCount < MAX_FIELDS &&
                            global.reportSize != 0 &&
                            global.reportSize <= 32
                        ) {
                            HidField& field =
                                state.fields[state.fieldCount++];

                            field.used = true;
                            field.reportId = global.reportId;
                            field.bitOffset = bitOffset;
                            field.bitSize = global.reportSize;
                            field.usagePage = global.usagePage;
                            field.usage = localUsage(local, i);
                            field.logicalMin = global.logicalMin;
                            field.logicalMax = global.logicalMax;
                        }

                        bitOffset = static_cast<uint16_t>(
                            bitOffset + global.reportSize
                        );
                    }
                } else {
                    bitOffset = static_cast<uint16_t>(
                        bitOffset +
                        static_cast<uint16_t>(global.reportSize) *
                        static_cast<uint16_t>(global.reportCount)
                    );
                }

                clearLocal(local);
                continue;
            }

            // COLLECTION
            if (tag == 10) {
                const uint16_t usage =
                    localUsage(local, 0);
                const uint8_t newDepth =
                    static_cast<uint8_t>(collectionDepth + 1);

                if (
                    collectionDepth == 0 &&
                    gamepadUsage(global.usagePage, usage)
                ) {
                    state.isGamepad = true;
                    state.topUsagePage = global.usagePage;
                    state.topUsage = usage;
                    gamepadCollectionDepth = newDepth;
                }

                collectionDepth = newDepth;
                clearLocal(local);
                continue;
            }

            // END COLLECTION
            if (tag == 12) {
                if (collectionDepth == gamepadCollectionDepth) {
                    gamepadCollectionDepth = 0;
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

        // Global items.
        if (type == 1) {
            switch (tag) {
                case 0:
                    global.usagePage =
                        static_cast<uint16_t>(unsignedValue);
                    break;

                case 1:
                    global.logicalMin = signedValue;
                    break;

                case 2:
                    global.logicalMax =
                        global.logicalMin < 0
                            ? signedValue
                            : static_cast<int32_t>(unsignedValue);
                    break;

                case 7:
                    global.reportSize =
                        static_cast<uint8_t>(unsignedValue);
                    break;

                case 8:
                    global.reportId =
                        static_cast<uint8_t>(unsignedValue);
                    break;

                case 9:
                    global.reportCount =
                        static_cast<uint8_t>(unsignedValue);
                    break;

                case 10:
                    if (globalStackCount < GLOBAL_STACK_DEPTH) {
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

        // Local items.
        if (type == 2) {
            switch (tag) {
                case 0:
                    if (local.usageCount < MAX_LOCAL_USAGES) {
                        local.usages[local.usageCount++] =
                            static_cast<uint16_t>(unsignedValue);
                    }
                    break;

                case 1:
                    local.hasUsageMin = true;
                    local.usageMin =
                        static_cast<uint16_t>(unsignedValue);
                    break;

                case 2:
                    local.hasUsageMax = true;
                    local.usageMax =
                        static_cast<uint16_t>(unsignedValue);
                    break;

                default:
                    break;
            }
        }
    }

    if (!state.isGamepad || state.fieldCount == 0) {
        state.isGamepad = false;
        return;
    }

    UniversalUsbProbe probe {};
    probe.vid = state.vid;
    probe.pid = state.pid;
    probe.interfaceClass = 0x03;
    probe.interfaceSubClass = 0x00;
    probe.interfaceProtocol = state.protocol;
    probe.endpointCount = 1;
    probe.hidUsagePage = state.topUsagePage;
    probe.hidUsage = state.topUsage;

    state.device = UDEVREG.classifyUsb(probe);

    if (!state.device.recognized) {
        return;
    }

    state.globalSlot =
        UINPUT.claimUsbSlot(
            UniversalInputSource::USB_HID_GAMEPAD,
            state.device,
            state.devAddr,
            state.instance
        );
}

uint32_t UniversalHIDGamepadHostAddon::extractBits(
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

    for (uint8_t bit = 0; bit < bitSize; bit++) {
        const uint32_t absoluteBit =
            static_cast<uint32_t>(bitOffset) + bit;
        const uint16_t byteIndex =
            static_cast<uint16_t>(absoluteBit / 8u);

        if (byteIndex >= dataLength) {
            break;
        }

        const uint8_t bitIndex =
            static_cast<uint8_t>(absoluteBit % 8u);

        if (data[byteIndex] & (1u << bitIndex)) {
            value |= 1u << bit;
        }
    }

    return value;
}

uint16_t UniversalHIDGamepadHostAddon::scaleAxis(
    int32_t value,
    int32_t logicalMin,
    int32_t logicalMax
) {
    if (logicalMax <= logicalMin) {
        return GAMEPAD_JOYSTICK_MID;
    }

    if (value < logicalMin) value = logicalMin;
    if (value > logicalMax) value = logicalMax;

    const int64_t numerator =
        static_cast<int64_t>(value - logicalMin) *
        GAMEPAD_JOYSTICK_MAX;

    return static_cast<uint16_t>(
        numerator / (logicalMax - logicalMin)
    );
}

uint8_t UniversalHIDGamepadHostAddon::scaleTrigger(
    int32_t value,
    int32_t logicalMin,
    int32_t logicalMax
) {
    if (logicalMax <= logicalMin) {
        return 0;
    }

    if (value < logicalMin) value = logicalMin;
    if (value > logicalMax) value = logicalMax;

    const int64_t numerator =
        static_cast<int64_t>(value - logicalMin) * 255;

    return static_cast<uint8_t>(
        numerator / (logicalMax - logicalMin)
    );
}

void UniversalHIDGamepadHostAddon::applyButton(
    GamepadState& state,
    uint16_t usage,
    bool pressed
) {
    if (!pressed) {
        return;
    }

    // Common DirectInput / generic-HID ordering.
    switch (usage) {
        case 1:  state.buttons |= GAMEPAD_MASK_B1; break;
        case 2:  state.buttons |= GAMEPAD_MASK_B2; break;
        case 3:  state.buttons |= GAMEPAD_MASK_B3; break;
        case 4:  state.buttons |= GAMEPAD_MASK_B4; break;
        case 5:  state.buttons |= GAMEPAD_MASK_L1; break;
        case 6:  state.buttons |= GAMEPAD_MASK_R1; break;
        case 7:  state.buttons |= GAMEPAD_MASK_L2; break;
        case 8:  state.buttons |= GAMEPAD_MASK_R2; break;
        case 9:  state.buttons |= GAMEPAD_MASK_S1; break;
        case 10: state.buttons |= GAMEPAD_MASK_S2; break;
        case 11: state.buttons |= GAMEPAD_MASK_L3; break;
        case 12: state.buttons |= GAMEPAD_MASK_R3; break;
        case 13: state.buttons |= GAMEPAD_MASK_A1; break;
        case 14: state.buttons |= GAMEPAD_MASK_A2; break;
        default: break;
    }
}

uint8_t UniversalHIDGamepadHostAddon::hatToDpad(
    int32_t value,
    int32_t logicalMin,
    int32_t logicalMax
) {
    int32_t index = -1;

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
        const int32_t range =
            logicalMax - logicalMin + 1;

        index =
            ((value - logicalMin) * 8) / range;
    }

    switch (index) {
        case 0: return GAMEPAD_MASK_UP;
        case 1: return GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT;
        case 2: return GAMEPAD_MASK_RIGHT;
        case 3: return GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT;
        case 4: return GAMEPAD_MASK_DOWN;
        case 5: return GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT;
        case 6: return GAMEPAD_MASK_LEFT;
        case 7: return GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT;
        default: return 0;
    }
}

void UniversalHIDGamepadHostAddon::processReport(
    InterfaceState& state
) {
    if (
        !state.parsed ||
        !state.isGamepad ||
        state.fieldCount == 0 ||
        state.reportLength == 0 ||
        state.globalSlot == UNIVERSAL_INPUT_SLOT_INVALID
    ) {
        return;
    }

    bool usesReportIds = false;

    for (uint8_t i = 0; i < state.fieldCount; i++) {
        if (state.fields[i].reportId != 0) {
            usesReportIds = true;
            break;
        }
    }

    uint8_t incomingReportId = 0;
    uint8_t const* payload = state.report;
    uint16_t payloadLength = state.reportLength;

    if (usesReportIds) {
        incomingReportId = state.report[0];
        payload = &state.report[1];
        payloadLength--;

        if (payloadLength == 0) {
            return;
        }
    }

    GamepadState out {};

    bool hasX = false;
    bool hasY = false;
    bool hasRx = false;
    bool hasRy = false;

    bool hasZ = false;
    bool hasRz = false;

    int32_t zValue = 0;
    int32_t zMin = 0;
    int32_t zMax = 0;

    int32_t rzValue = 0;
    int32_t rzMin = 0;
    int32_t rzMax = 0;

    for (uint8_t i = 0; i < state.fieldCount; i++) {
        HidField const& field = state.fields[i];

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
            signedFieldValue(field, raw);

        if (field.usagePage == USAGE_PAGE_BUTTON) {
            applyButton(out, field.usage, value != 0);
            continue;
        }

        if (field.usagePage != USAGE_PAGE_GENERIC_DESKTOP) {
            continue;
        }

        switch (field.usage) {
            case USAGE_X:
                out.lx = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasX = true;
                break;

            case USAGE_Y:
                out.ly = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasY = true;
                break;

            case USAGE_RX:
                out.rx = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasRx = true;
                break;

            case USAGE_RY:
                out.ry = scaleAxis(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                hasRy = true;
                break;

            case USAGE_Z:
                hasZ = true;
                zValue = value;
                zMin = field.logicalMin;
                zMax = field.logicalMax;
                break;

            case USAGE_RZ:
                hasRz = true;
                rzValue = value;
                rzMin = field.logicalMin;
                rzMax = field.logicalMax;
                break;

            case USAGE_HAT:
                out.dpad = hatToDpad(
                    value,
                    field.logicalMin,
                    field.logicalMax
                );
                break;

            default:
                break;
        }
    }

    // Common HID convention: signed centered Z/Rz can be a right stick.
    if (
        !hasRx &&
        !hasRy &&
        hasZ &&
        hasRz &&
        zMin < 0 &&
        rzMin < 0
    ) {
        out.rx = scaleAxis(zValue, zMin, zMax);
        out.ry = scaleAxis(rzValue, rzMin, rzMax);
        hasRx = true;
        hasRy = true;
    } else {
        if (hasZ && zMin >= 0) {
            out.lt = scaleTrigger(zValue, zMin, zMax);
            if (out.lt != 0) {
                out.buttons |= GAMEPAD_MASK_L2;
            }
        }

        if (hasRz && rzMin >= 0) {
            out.rt = scaleTrigger(rzValue, rzMin, rzMax);
            if (out.rt != 0) {
                out.buttons |= GAMEPAD_MASK_R2;
            }
        }
    }

    out.dpadOriginal = out.dpad;

    const bool useful =
        hasX ||
        hasY ||
        hasRx ||
        hasRy ||
        hasZ ||
        hasRz ||
        out.dpad != 0 ||
        out.buttons != 0;

    // After the first valid state, neutral reports are required for releases.
    if (!useful && !state.lastStateValid) {
        return;
    }

    if (
        state.lastStateValid &&
        memcmp(
            &state.lastState,
            &out,
            sizeof(GamepadState)
        ) == 0
    ) {
        return;
    }

    state.lastState = out;
    state.lastStateValid = true;

    UINPUT.publish(state.globalSlot, out);
}
