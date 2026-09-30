#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oag {

constexpr std::size_t kDiamondControllerSlots = 8;
constexpr std::size_t kDiamondGameSlots = 8;
constexpr std::size_t kDiamondWeaponSlotsPerGame = 24;
constexpr std::size_t kDiamondComboSlots = 16;
constexpr std::size_t kDiamondActionBindings = 48;

struct StickCalibration {
    std::int32_t centerX = 0;
    std::int32_t centerY = 0;
    std::uint32_t deadzone = 1800;
};

struct ControllerCalibration {
    bool enabled = true;
    StickCalibration left {};
    StickCalibration right {};
};

struct WeaponRecoilProfile {
    bool enabled = false;
    std::uint16_t tickMs = 40;
    // Half-permille preserves .5 values exactly without floating point.
    std::int32_t horizontalHalfPermille = 0;
    std::int32_t verticalHalfPermille = 0;
};

struct GameProfile {
    bool enabled = false;
    std::array<WeaponRecoilProfile, kDiamondWeaponSlotsPerGame> weapons {};
};

enum class DiamondActionId : std::uint8_t {
    None = 0,
    Combo1, Combo2, Combo3, Combo4, Combo5, Combo6, Combo7, Combo8,
    Weapon1, Weapon2, Weapon3, Weapon4, Weapon5, Weapon6, Weapon7, Weapon8,
};

enum class DiamondTriggerKind : std::uint8_t {
    KeyboardUsage = 0,
    MouseButton,
    GamepadButton,
};

struct DiamondActionBinding {
    bool enabled = false;
    DiamondActionId action = DiamondActionId::None;
    DiamondTriggerKind kind = DiamondTriggerKind::KeyboardUsage;
    std::uint16_t code = 0;
    std::uint8_t modifiers = 0;
};

struct DiamondRuntimeConfig {
    static constexpr std::uint32_t kMagic = 0x4449414Du; // DIAM
    static constexpr std::uint16_t kSchemaVersion = 1;

    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t activeGame = 0;
    std::uint16_t activeWeapon = 0;

    std::array<ControllerCalibration, kDiamondControllerSlots> controllers {};
    std::array<GameProfile, kDiamondGameSlots> games {};
    std::array<DiamondActionBinding, kDiamondActionBindings> bindings {};
};

} // namespace oag
