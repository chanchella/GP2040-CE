#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_config.h"

namespace oag {

constexpr std::size_t kDiamondAdminUsernameBytes = 25;
constexpr std::size_t kDiamondWifiPasswordBytes = 64;
constexpr std::size_t kDiamondPasswordSaltBytes = 16;
constexpr std::size_t kDiamondPasswordVerifierBytes = 32;
constexpr std::size_t kDiamondDisplayNameBytes = 33; // 32 chars + NUL
constexpr std::size_t kDiamondComboSteps = 16;

struct DiamondComboTiming {
    bool enabled = false;
    std::uint16_t pressMs = 50;
    std::uint16_t delayAfterMs = 50;
    std::uint16_t repeatCount = 1;
};

enum class DiamondLogicalControl : std::uint8_t {
    None = 0,
    South,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    LeftTrigger,
    RightTrigger,
    LeftStickClick,
    RightStickClick,
    Back,
    Start,
    Guide,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
};

enum class DiamondComboActivationMode : std::uint8_t {
    WhileHeld = 0,
    PressOnce,
    Toggle,
};

enum class DiamondComboRepeatMode : std::uint8_t {
    Once = 0,
    AutoRepeat,
};

enum class DiamondComboStepKind : std::uint8_t {
    Press = 0,
    HoldStart,
    HoldEnd,
    Pulse,
    Wait,
    WaitUntilPressed,
    WaitUntilReleased,
};

struct DiamondComboStep {
    bool enabled = false;
    DiamondComboStepKind kind = DiamondComboStepKind::Press;
    DiamondLogicalControl control = DiamondLogicalControl::None;

    // Press: how long the control remains down.
    // Wait: delay duration.
    // Pulse: down-time of every pulse.
    std::uint16_t durationMs = 50;

    // Pulse only: time from the end of one pulse to the next pulse.
    std::uint16_t intervalMs = 50;

    // Pulse only. 0 means keep pulsing until the combo is cancelled.
    std::uint16_t repeatCount = 1;
};

struct DiamondComboProgram {
    bool enabled = false;
    DiamondComboActivationMode activation =
        DiamondComboActivationMode::WhileHeld;
    DiamondComboRepeatMode repeat = DiamondComboRepeatMode::Once;

    // If false, the trigger is consumed while the combo is active. If true,
    // the player's original input continues beside the generated actions.
    bool passTriggerThrough = true;

    bool cancelOnTriggerRelease = true;
    bool cancelOnTriggerPressAgain = false;
    bool cancelControlEnabled = false;
    DiamondLogicalControl cancelControl = DiamondLogicalControl::None;

    std::uint8_t stepCount = 0;
    std::array<DiamondComboStep, kDiamondComboSteps> steps {};
};

struct DiamondSecurityConfig {
    bool provisioned = false;
    std::array<char, kDiamondAdminUsernameBytes> adminUsername {};
    std::array<char, kDiamondWifiPasswordBytes> wifiPassword {};
    std::array<std::uint8_t, kDiamondPasswordSaltBytes> passwordSalt {};
    std::array<std::uint8_t, kDiamondPasswordVerifierBytes> passwordVerifier {};
    std::uint32_t passwordIterations = 12000;
};

struct DiamondContentNames {
    std::array<std::array<char, kDiamondDisplayNameBytes>, kDiamondGameSlots> games {};
    std::array<
        std::array<
            std::array<char, kDiamondDisplayNameBytes>,
            kDiamondWeaponSlotsPerGame
        >,
        kDiamondGameSlots
    > weapons {};
    std::array<std::array<char, kDiamondDisplayNameBytes>, kDiamondComboSlots> combos {};

    // Retained for transparent migration/compatibility with the first
    // millisecond timing implementation. New combos use comboPrograms.
    std::array<DiamondComboTiming, kDiamondComboSlots> comboTiming {};
    std::array<DiamondComboProgram, kDiamondComboSlots> comboPrograms {};
};

struct DiamondPersistentConfig {
    static constexpr std::uint32_t kMagic = 0x4F414750u; // OAGP
    static constexpr std::uint16_t kSchemaVersion = 4;

    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    DiamondRuntimeConfig runtime {};
    DiamondSecurityConfig security {};
    DiamondContentNames names {};
};

} // namespace oag
