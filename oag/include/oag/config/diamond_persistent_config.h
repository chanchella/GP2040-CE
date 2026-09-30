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
};

struct DiamondPersistentConfig {
    static constexpr std::uint32_t kMagic = 0x4F414750u; // OAGP
    static constexpr std::uint16_t kSchemaVersion = 2;

    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    DiamondRuntimeConfig runtime {};
    DiamondSecurityConfig security {};
    DiamondContentNames names {};
};

} // namespace oag
