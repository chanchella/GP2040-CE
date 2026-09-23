#ifndef _UNIVERSAL_HUMAN_INTERFACE_MANAGER_H_
#define _UNIVERSAL_HUMAN_INTERFACE_MANAGER_H_

#include <stdint.h>

#include "input/universal_device_registry.h"
#include "pico/sync.h"

static constexpr uint8_t UNIVERSAL_HID_SLOT_BLUETOOTH = 0;
static constexpr uint8_t UNIVERSAL_HID_SLOT_USB_1 = 1;
static constexpr uint8_t UNIVERSAL_HID_SLOT_USB_2 = 2;
static constexpr uint8_t UNIVERSAL_HID_SLOT_USB_3 = 3;
static constexpr uint8_t UNIVERSAL_HID_SLOT_COUNT = 4;
static constexpr uint8_t UNIVERSAL_HID_SLOT_INVALID = 0xFF;

static constexpr uint8_t UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS = 8;
static constexpr uint8_t UNIVERSAL_KEYBOARD_MAX_CONSUMER_USAGES = 16;

enum class UniversalHumanInterfaceSource : uint8_t {
    NONE = 0,
    BLUETOOTH_LE_HID,
    BLUETOOTH_CLASSIC_HID,
    USB_HID,
    USB_2_4GHZ_HID,
};

struct UniversalKeyboardState {
    // HID Keyboard/Keypad usage page 0x07: usages 0..255.
    // Eight 32-bit words preserve full NKRO state without boot-protocol
    // six-key rollover limits.
    uint32_t keys[UNIVERSAL_KEYBOARD_USAGE_BITMAP_WORDS] {};

    // Standard HID modifier bitmap, bits 0..7 = LeftCtrl..RightGUI.
    uint8_t modifiers = 0;

    // Current pressed usages from Consumer page 0x0C. This is intentionally
    // usage-based rather than mapped early to media actions.
    uint16_t consumerUsages[UNIVERSAL_KEYBOARD_MAX_CONSUMER_USAGES] {};
    uint8_t consumerUsageCount = 0;

    bool isKeyDown(uint8_t usage) const {
        const uint8_t word = static_cast<uint8_t>(usage >> 5);
        const uint8_t bit = static_cast<uint8_t>(usage & 31);
        return (keys[word] & (1u << bit)) != 0;
    }

    void setKeyDown(uint8_t usage, bool down) {
        const uint8_t word = static_cast<uint8_t>(usage >> 5);
        const uint8_t bit = static_cast<uint8_t>(usage & 31);
        const uint32_t mask = 1u << bit;

        if (down) {
            keys[word] |= mask;
        } else {
            keys[word] &= ~mask;
        }
    }
};

struct UniversalMouseState {
    // Relative motion is kept at 32-bit width so 8/12/16-bit HID reports and
    // high-resolution wheels can be normalized without truncation.
    int32_t x = 0;
    int32_t y = 0;
    int32_t wheel = 0;
    int32_t horizontalWheel = 0;

    // Bit 0 = Button 1. Reserve 32 buttons while guaranteeing at least 1..8.
    uint32_t buttons = 0;
};

struct UniversalKeyboardSlotSnapshot {
    bool connected = false;
    bool hasReport = false;

    UniversalHumanInterfaceSource source =
        UniversalHumanInterfaceSource::NONE;
    UniversalTransport transport = UniversalTransport::UNKNOWN;
    UniversalProtocol protocol = UniversalProtocol::UNKNOWN;

    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t devAddr = 0;
    uint8_t instance = 0;

    uint32_t generation = 0;
    UniversalKeyboardState state {};
};

struct UniversalMouseSlotSnapshot {
    bool connected = false;
    bool hasReport = false;

    UniversalHumanInterfaceSource source =
        UniversalHumanInterfaceSource::NONE;
    UniversalTransport transport = UniversalTransport::UNKNOWN;
    UniversalProtocol protocol = UniversalProtocol::UNKNOWN;

    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t devAddr = 0;
    uint8_t instance = 0;

    uint32_t generation = 0;
    UniversalMouseState state {};
};

class UniversalHumanInterfaceManager {
public:
    UniversalHumanInterfaceManager(
        UniversalHumanInterfaceManager const&
    ) = delete;
    void operator=(UniversalHumanInterfaceManager const&) = delete;

    static UniversalHumanInterfaceManager& getInstance();

    void resetAll();

    bool connectKeyboard(
        uint8_t slot,
        UniversalHumanInterfaceSource source,
        UniversalTransport transport,
        UniversalProtocol protocol,
        uint16_t vid,
        uint16_t pid,
        uint8_t devAddr,
        uint8_t instance
    );

    bool connectMouse(
        uint8_t slot,
        UniversalHumanInterfaceSource source,
        UniversalTransport transport,
        UniversalProtocol protocol,
        uint16_t vid,
        uint16_t pid,
        uint8_t devAddr,
        uint8_t instance
    );

    uint8_t claimUsbKeyboardSlot(
        UniversalHumanInterfaceSource source,
        UniversalTransport transport,
        uint16_t vid,
        uint16_t pid,
        uint8_t devAddr,
        uint8_t instance
    );

    uint8_t claimUsbMouseSlot(
        UniversalHumanInterfaceSource source,
        UniversalTransport transport,
        uint16_t vid,
        uint16_t pid,
        uint8_t devAddr,
        uint8_t instance
    );

    void disconnectKeyboard(uint8_t slot);
    void disconnectMouse(uint8_t slot);

    void releaseUsbKeyboardDevice(
        UniversalHumanInterfaceSource source,
        uint8_t devAddr
    );

    void releaseUsbMouseDevice(
        UniversalHumanInterfaceSource source,
        uint8_t devAddr
    );

    bool publishKeyboard(
        uint8_t slot,
        UniversalKeyboardState const& state
    );

    bool publishMouse(
        uint8_t slot,
        UniversalMouseState const& state
    );

    bool snapshotKeyboard(
        uint8_t slot,
        UniversalKeyboardSlotSnapshot& out
    ) const;

    bool snapshotMouse(
        uint8_t slot,
        UniversalMouseSlotSnapshot& out
    ) const;

private:
    UniversalHumanInterfaceManager();

    struct KeyboardSlot {
        bool connected = false;
        bool hasReport = false;
        UniversalHumanInterfaceSource source =
            UniversalHumanInterfaceSource::NONE;
        UniversalTransport transport = UniversalTransport::UNKNOWN;
        UniversalProtocol protocol = UniversalProtocol::UNKNOWN;
        uint16_t vid = 0;
        uint16_t pid = 0;
        uint8_t devAddr = 0;
        uint8_t instance = 0;
        uint32_t generation = 0;
        UniversalKeyboardState state {};
    };

    struct MouseSlot {
        bool connected = false;
        bool hasReport = false;
        UniversalHumanInterfaceSource source =
            UniversalHumanInterfaceSource::NONE;
        UniversalTransport transport = UniversalTransport::UNKNOWN;
        UniversalProtocol protocol = UniversalProtocol::UNKNOWN;
        uint16_t vid = 0;
        uint16_t pid = 0;
        uint8_t devAddr = 0;
        uint8_t instance = 0;
        uint32_t generation = 0;
        UniversalMouseState state {};
    };

    mutable critical_section_t lock;
    KeyboardSlot keyboardSlots[UNIVERSAL_HID_SLOT_COUNT] {};
    MouseSlot mouseSlots[UNIVERSAL_HID_SLOT_COUNT] {};

    static bool validSlot(uint8_t slot) {
        return slot < UNIVERSAL_HID_SLOT_COUNT;
    }

    void resetKeyboardSlotUnlocked(uint8_t slot);
    void resetMouseSlotUnlocked(uint8_t slot);
};

#define UHIDINPUT UniversalHumanInterfaceManager::getInstance()

#endif
