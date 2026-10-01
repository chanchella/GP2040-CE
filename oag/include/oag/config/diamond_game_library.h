#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_persistent_config.h"

namespace oag {

constexpr std::size_t kDiamondLibraryGameSlots = 20;
constexpr std::uint16_t kDiamondNoActiveWeapon = 0xFFFFu;

enum class WeaponRecoilCurve : std::uint8_t {
    Direct = 0,
    Linear = 1,
    EaseIn = 2,
};

// Advanced per-weapon tuning is intentionally stored beside the game library,
// not in the frozen core runtime config. This keeps the proven controller /
// Bluetooth / USB / XInput config binary-compatible with the RS8 V2 stock.
struct WeaponTuningProfile {
    bool adsOverrideEnabled = false;
    bool syncTickToFireRate = false;
    bool firstShotEnabled = false;
    std::uint8_t antiShakePercent = 0;
    std::uint8_t smoothingPercent = 0;
    WeaponRecoilCurve recoilCurve = WeaponRecoilCurve::Direct;
    std::uint8_t reserved0 = 0;

    std::uint16_t fireRateRpm = 600;
    std::uint16_t reloadMs = 2500;
    std::uint16_t startDelayMs = 0;
    std::uint16_t rampDurationMs = 0;

    std::int16_t adsHorizontalRaw = 0;
    std::int16_t adsVerticalRaw = 0;
    std::int16_t firstShotHorizontalRaw = 0;
    std::int16_t firstShotVerticalRaw = 0;
};

struct DiamondGameContent {
    std::array<char, kDiamondDisplayNameBytes> gameName {};
    std::array<
        std::array<char, kDiamondDisplayNameBytes>,
        kDiamondWeaponSlotsPerGame
    > weaponNames {};
    std::array<WeaponRecoilProfile, kDiamondWeaponSlotsPerGame> weapons {};

    std::array<
        std::array<char, kDiamondDisplayNameBytes>,
        kDiamondComboSlots
    > comboNames {};
    std::array<DiamondComboTiming, kDiamondComboSlots> comboTiming {};
    std::array<DiamondComboProgram, kDiamondComboSlots> comboPrograms {};

    // RS8 V2 Weapon Tuning Experimental V1 extension. Appended so a V1 game
    // record can be migrated losslessly by the firmware store.
    std::array<WeaponTuningProfile, kDiamondWeaponSlotsPerGame> weaponTuning {};
};

struct DiamondGameRecord {
    static constexpr std::uint32_t kMagic = 0x4F414747u; // OAGG
    static constexpr std::uint16_t kRecordVersion = 2;

    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t gameIndex = 0;
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    DiamondGameContent payload {};
};

static_assert(
    sizeof(DiamondGameRecord) <= 16384,
    "One OAG Game must fit in its 16 KiB A/B Flash record"
);

} // namespace oag
