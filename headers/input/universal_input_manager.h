#ifndef _UNIVERSAL_INPUT_MANAGER_H_
#define _UNIVERSAL_INPUT_MANAGER_H_

#include <stdint.h>

#include "gamepad/GamepadState.h"
#include "pico/sync.h"

// Global controller-slot namespace for the Universal Input Dongle.
//
// Slot 0 is reserved for Bluetooth so the proven Bluetooth path can be
// integrated later without colliding with USB controllers.
// Slots 1..3 are physical/logical USB controller slots.
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_BLUETOOTH = 0;
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_USB_1 = 1;
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_USB_2 = 2;
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_USB_3 = 3;
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_COUNT = 4;
static constexpr uint8_t UNIVERSAL_INPUT_SLOT_INVALID = 0xFF;

enum class UniversalInputSource : uint8_t {
    NONE = 0,
    BLUETOOTH_GAMEPAD,
    USB_XINPUT,
    USB_HID_GAMEPAD,
};

struct UniversalInputSlotSnapshot {
    bool connected = false;
    bool hasReport = false;

    UniversalInputSource source = UniversalInputSource::NONE;

    uint16_t vid = 0;
    uint16_t pid = 0;

    uint8_t devAddr = 0;
    uint8_t instance = 0;

    uint32_t generation = 0;

    GamepadState state {};
};

class UniversalInputManager {
public:
    UniversalInputManager(UniversalInputManager const&) = delete;
    void operator=(UniversalInputManager const&) = delete;

    static UniversalInputManager& getInstance();

    void resetAll();

    bool connect(
        uint8_t slot,
        UniversalInputSource source,
        uint16_t vid,
        uint16_t pid,
        uint8_t devAddr,
        uint8_t instance
    );

    void disconnect(uint8_t slot);

    bool publish(uint8_t slot, GamepadState const& state);

    bool snapshot(
        uint8_t slot,
        UniversalInputSlotSnapshot& out
    ) const;

private:
    UniversalInputManager();

    struct Slot {
        bool connected = false;
        bool hasReport = false;

        UniversalInputSource source = UniversalInputSource::NONE;

        uint16_t vid = 0;
        uint16_t pid = 0;

        uint8_t devAddr = 0;
        uint8_t instance = 0;

        uint32_t generation = 0;

        GamepadState state {};
    };

    mutable critical_section_t lock;
    Slot slots[UNIVERSAL_INPUT_SLOT_COUNT];

    static bool validSlot(uint8_t slot) {
        return slot < UNIVERSAL_INPUT_SLOT_COUNT;
    }

    void resetSlotUnlocked(uint8_t slot);
};

#define UINPUT UniversalInputManager::getInstance()

#endif
