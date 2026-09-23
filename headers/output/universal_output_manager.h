#ifndef _UNIVERSAL_OUTPUT_MANAGER_H_
#define _UNIVERSAL_OUTPUT_MANAGER_H_

#include <stdint.h>

#include "gamepad/GamepadState.h"
#include "input/universal_input_manager.h"
#include "pico/sync.h"

static constexpr uint8_t UNIVERSAL_OUTPUT_SLOT_0 = 0;
static constexpr uint8_t UNIVERSAL_OUTPUT_SLOT_1 = 1;
static constexpr uint8_t UNIVERSAL_OUTPUT_SLOT_2 = 2;
static constexpr uint8_t UNIVERSAL_OUTPUT_SLOT_3 = 3;
static constexpr uint8_t UNIVERSAL_OUTPUT_SLOT_COUNT = 4;

struct UniversalOutputSlotSnapshot {
    bool connected = false;
    bool hasReport = false;

    uint8_t inputSlot = UNIVERSAL_INPUT_SLOT_INVALID;
    UniversalInputSource source = UniversalInputSource::NONE;

    UniversalTransport transport = UniversalTransport::UNKNOWN;
    UniversalDeviceClass deviceClass = UniversalDeviceClass::UNKNOWN;
    UniversalProtocol protocol = UniversalProtocol::UNKNOWN;
    UniversalDriverFamily driverFamily = UniversalDriverFamily::NONE;
    UniversalDeviceProfileId profile = UniversalDeviceProfileId::NONE;
    uint32_t quirks = UNIVERSAL_QUIRK_NONE;
    uint32_t capabilities = UNIVERSAL_CAP_NONE;
    uint32_t verifiedCapabilities = UNIVERSAL_CAP_NONE;

    uint16_t vid = 0;
    uint16_t pid = 0;

    uint32_t generation = 0;
    uint32_t inputGeneration = 0;

    GamepadState state {};
};

class UniversalOutputManager {
public:
    UniversalOutputManager(UniversalOutputManager const&) = delete;
    void operator=(UniversalOutputManager const&) = delete;

    static UniversalOutputManager& getInstance();

    void resetAll();

    // G2B/G2C0 policy: deterministic 1:1 routing.
    // Input Slot N -> Logical Output Slot N.
    void syncFromInputs();

    bool snapshot(
        uint8_t outputSlot,
        UniversalOutputSlotSnapshot& out
    ) const;

private:
    UniversalOutputManager();

    struct Slot {
        bool connected = false;
        bool hasReport = false;

        uint8_t inputSlot = UNIVERSAL_INPUT_SLOT_INVALID;
        UniversalInputSource source = UniversalInputSource::NONE;

        UniversalTransport transport = UniversalTransport::UNKNOWN;
        UniversalDeviceClass deviceClass = UniversalDeviceClass::UNKNOWN;
        UniversalProtocol protocol = UniversalProtocol::UNKNOWN;
        UniversalDriverFamily driverFamily = UniversalDriverFamily::NONE;
        UniversalDeviceProfileId profile = UniversalDeviceProfileId::NONE;
        uint32_t quirks = UNIVERSAL_QUIRK_NONE;
        uint32_t capabilities = UNIVERSAL_CAP_NONE;
        uint32_t verifiedCapabilities = UNIVERSAL_CAP_NONE;

        uint16_t vid = 0;
        uint16_t pid = 0;

        uint32_t generation = 0;
        uint32_t inputGeneration = 0;

        GamepadState state {};
    };

    mutable critical_section_t lock;
    Slot slots[UNIVERSAL_OUTPUT_SLOT_COUNT];

    static bool validSlot(uint8_t slot) {
        return slot < UNIVERSAL_OUTPUT_SLOT_COUNT;
    }

    void clearSlotUnlocked(uint8_t slot);
    void publishFromInputUnlocked(
        uint8_t outputSlot,
        uint8_t inputSlot,
        UniversalInputSlotSnapshot const& input
    );
};

#define UOUTPUT UniversalOutputManager::getInstance()

#endif
