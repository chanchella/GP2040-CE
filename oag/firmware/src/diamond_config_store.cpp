#include "oag/firmware/diamond_config_store.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/platform.h"
#include "pico/btstack_flash_bank.h"

namespace {

constexpr std::uint32_t kSlotSize = 4u * FLASH_SECTOR_SIZE; // 16 KiB
constexpr std::uint32_t kSlotBOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - kSlotSize;
constexpr std::uint32_t kSlotAOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * kSlotSize);

// Previous V1 records occupied the two sectors immediately before BTstack.
// They are read-only migration sources until the first V2 saves complete.
constexpr std::uint32_t kLegacySlotBOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
constexpr std::uint32_t kLegacySlotAOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * FLASH_SECTOR_SIZE);

static_assert(PICO_FLASH_BANK_STORAGE_OFFSET >= (2u * kSlotSize));
static_assert((kSlotAOffset % FLASH_SECTOR_SIZE) == 0);
static_assert((kSlotBOffset % FLASH_SECTOR_SIZE) == 0);
static_assert(kSlotBOffset + kSlotSize <= PICO_FLASH_BANK_STORAGE_OFFSET);
static_assert(sizeof(oag::DiamondConfigRecord) <= kSlotSize);

extern "C" std::uint8_t __flash_binary_end;

struct LegacyContentNamesV2 {
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondGameSlots> games {};
    std::array<
        std::array<
            std::array<char, oag::kDiamondDisplayNameBytes>,
            oag::kDiamondWeaponSlotsPerGame
        >,
        oag::kDiamondGameSlots
    > weapons {};
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondComboSlots> combos {};
};

struct LegacyPersistentConfigV2 {
    static constexpr std::uint32_t kMagic = 0x4F414750u;
    static constexpr std::uint16_t kSchemaVersion = 2;
    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    oag::DiamondRuntimeConfig runtime {};
    oag::DiamondSecurityConfig security {};
    LegacyContentNamesV2 names {};
};

struct LegacyConfigRecordV2 {
    static constexpr std::uint32_t kMagic = 0x4F414743u;
    static constexpr std::uint16_t kRecordVersion = 2;
    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t payloadLength = sizeof(LegacyPersistentConfigV2);
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    LegacyPersistentConfigV2 payload {};
};

static_assert(sizeof(LegacyConfigRecordV2) <= kSlotSize);

struct LegacyPersistentConfigV1 {
    static constexpr std::uint32_t kMagic = 0x4F414750u;
    static constexpr std::uint16_t kSchemaVersion = 1;
    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    oag::DiamondRuntimeConfig runtime {};
    oag::DiamondSecurityConfig security {};
};

struct LegacyConfigRecordV1 {
    static constexpr std::uint32_t kMagic = 0x4F414743u;
    static constexpr std::uint16_t kRecordVersion = 1;
    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t payloadLength = sizeof(LegacyPersistentConfigV1);
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    LegacyPersistentConfigV1 payload {};
};

static_assert(sizeof(LegacyConfigRecordV1) <= FLASH_SECTOR_SIZE);

struct FlashWriteContext {
    std::uint32_t offset = 0;
    const std::uint8_t* data = nullptr;
};

void __not_in_flash_func(writeConfigSlot)(void* raw) {
    const auto* context = static_cast<const FlashWriteContext*>(raw);
    flash_range_erase(context->offset, kSlotSize);
    flash_range_program(context->offset, context->data, kSlotSize);
}

const oag::DiamondConfigRecord* recordAt(std::uint32_t offset) {
    return reinterpret_cast<const oag::DiamondConfigRecord*>(XIP_BASE + offset);
}

const LegacyConfigRecordV2* legacyV2RecordAt(std::uint32_t offset) {
    return reinterpret_cast<const LegacyConfigRecordV2*>(XIP_BASE + offset);
}

const LegacyConfigRecordV1* legacyRecordAt(std::uint32_t offset) {
    return reinterpret_cast<const LegacyConfigRecordV1*>(XIP_BASE + offset);
}

bool validLegacyV2(const LegacyConfigRecordV2& record) {
    return
        record.magic == LegacyConfigRecordV2::kMagic &&
        record.recordVersion == LegacyConfigRecordV2::kRecordVersion &&
        record.payloadLength == sizeof(LegacyPersistentConfigV2) &&
        record.payload.magic == LegacyPersistentConfigV2::kMagic &&
        record.payload.schemaVersion == LegacyPersistentConfigV2::kSchemaVersion &&
        record.payload.runtime.magic == oag::DiamondRuntimeConfig::kMagic &&
        record.payload.runtime.schemaVersion == oag::DiamondRuntimeConfig::kSchemaVersion &&
        record.payloadCrc32 == oag::diamondConfigCrc32(
            &record.payload, sizeof(record.payload)
        );
}

const LegacyConfigRecordV2* selectLegacyV2() {
    const auto* a = legacyV2RecordAt(kSlotAOffset);
    const auto* b = legacyV2RecordAt(kSlotBOffset);
    const bool va = validLegacyV2(*a);
    const bool vb = validLegacyV2(*b);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;
    const auto delta = static_cast<std::int32_t>(b->generation - a->generation);
    return delta > 0 ? b : a;
}

bool validLegacy(const LegacyConfigRecordV1& record) {
    return
        record.magic == LegacyConfigRecordV1::kMagic &&
        record.recordVersion == LegacyConfigRecordV1::kRecordVersion &&
        record.payloadLength == sizeof(LegacyPersistentConfigV1) &&
        record.payload.magic == LegacyPersistentConfigV1::kMagic &&
        record.payload.schemaVersion == LegacyPersistentConfigV1::kSchemaVersion &&
        record.payload.runtime.magic == oag::DiamondRuntimeConfig::kMagic &&
        record.payload.runtime.schemaVersion == oag::DiamondRuntimeConfig::kSchemaVersion &&
        record.payloadCrc32 == oag::diamondConfigCrc32(
            &record.payload, sizeof(record.payload)
        );
}

const LegacyConfigRecordV1* selectLegacy() {
    const auto* a = legacyRecordAt(kLegacySlotAOffset);
    const auto* b = legacyRecordAt(kLegacySlotBOffset);
    const bool va = validLegacy(*a);
    const bool vb = validLegacy(*b);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;
    const auto delta = static_cast<std::int32_t>(b->generation - a->generation);
    return delta > 0 ? b : a;
}

bool firmwareLeavesConfigAreaFree() {
    const auto binaryEnd = reinterpret_cast<std::uintptr_t>(&__flash_binary_end);
    return binaryEnd <= (XIP_BASE + kSlotAOffset);
}

} // namespace

namespace oag::firmware {

bool DiamondConfigStore::load() {
    config_ = oag::DiamondPersistentConfig {};
    generation_ = 0;
    activeSlot_ = 0xFF;
    loadedFromFlash_ = false;

    if (!firmwareLeavesConfigAreaFree()) return false;

    const auto* slotA = recordAt(kSlotAOffset);
    const auto* slotB = recordAt(kSlotBOffset);
    const auto* selected = oag::selectNewestDiamondConfigRecord(slotA, slotB);

    if (selected != nullptr) {
        config_ = selected->payload;
        generation_ = selected->generation;
        activeSlot_ = selected == slotA ? 0u : 1u;
        loadedFromFlash_ = true;
        return true;
    }

    // Preserve all user-created OAG names when upgrading the V2 named-content
    // record to V3 combo timing storage. The old slot remains untouched until
    // the first successful V3 save writes the opposite slot.
    if (const auto* legacyV2 = selectLegacyV2(); legacyV2 != nullptr) {
        config_.runtime = legacyV2->payload.runtime;
        config_.security = legacyV2->payload.security;
        config_.names.games = legacyV2->payload.names.games;
        config_.names.weapons = legacyV2->payload.names.weapons;
        config_.names.combos = legacyV2->payload.names.combos;
        generation_ = legacyV2->generation;
        activeSlot_ = legacyV2 == legacyV2RecordAt(kSlotAOffset) ? 0u : 1u;
        loadedFromFlash_ = true;
        return true;
    }

    // One-time transparent migration from the hardware-proven V1 layout.
    if (const auto* legacy = selectLegacy(); legacy != nullptr) {
        config_.runtime = legacy->payload.runtime;
        config_.security = legacy->payload.security;
        generation_ = legacy->generation;
        loadedFromFlash_ = true;
    }
    return true;
}

bool DiamondConfigStore::save() {
    if (!firmwareLeavesConfigAreaFree()) return false;

    pendingRecord_ = oag::DiamondConfigRecord {};
    pendingRecord_.generation = generation_ + 1u;
    pendingRecord_.payload = config_;
    oag::finalizeDiamondConfigRecord(pendingRecord_);

    const std::uint8_t targetSlot = activeSlot_ == 0u ? 1u : 0u;
    const std::uint32_t targetOffset =
        targetSlot == 0u ? kSlotAOffset : kSlotBOffset;

    alignas(FLASH_PAGE_SIZE) static std::uint8_t slot[kSlotSize];
    std::memset(slot, 0xFF, sizeof(slot));
    std::memcpy(slot, &pendingRecord_, sizeof(pendingRecord_));

    FlashWriteContext context { targetOffset, slot };
    const int result = flash_safe_execute(writeConfigSlot, &context, 2500);
    if (result != PICO_OK) return false;

    const auto* written = recordAt(targetOffset);
    if (!oag::validateDiamondConfigRecord(*written)) return false;

    generation_ = pendingRecord_.generation;
    activeSlot_ = targetSlot;
    loadedFromFlash_ = true;
    return true;
}

} // namespace oag::firmware
