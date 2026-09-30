#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/config/diamond_persistent_config.h"

namespace oag {

constexpr std::size_t kDiamondLibraryGameSlots = 20;
constexpr std::uint16_t kDiamondNoActiveWeapon = 0xFFFFu;

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
};

struct DiamondGameRecord {
    static constexpr std::uint32_t kMagic = 0x4F414747u; // OAGG
    static constexpr std::uint16_t kRecordVersion = 1;

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
