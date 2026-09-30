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

constexpr std::uint32_t kSlotSize = 6u * FLASH_SECTOR_SIZE; // 24 KiB V5+

// V2-V4 used the final 32 KiB before BTstack. V5 slots live entirely before
// that legacy area so the first V5 save cannot erase the only migration
// source if power is lost during the write.
constexpr std::uint32_t kLegacyWideSlotSize = 4u * FLASH_SECTOR_SIZE;
constexpr std::uint32_t kLegacyWideSlotBOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - kLegacyWideSlotSize;
constexpr std::uint32_t kLegacyWideSlotAOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * kLegacyWideSlotSize);
constexpr std::uint32_t kLegacyWideAreaStart = kLegacyWideSlotAOffset;

constexpr std::uint32_t kSlotBOffset =
    kLegacyWideAreaStart - kSlotSize;
constexpr std::uint32_t kSlotAOffset =
    kLegacyWideAreaStart - (2u * kSlotSize);

// Previous V1 records occupied the two sectors immediately before BTstack.
// They are read-only migration sources until the first V2 saves complete.
constexpr std::uint32_t kLegacySlotBOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
constexpr std::uint32_t kLegacySlotAOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * FLASH_SECTOR_SIZE);

static_assert(kLegacyWideAreaStart >= (2u * kSlotSize));
static_assert((kSlotAOffset % FLASH_SECTOR_SIZE) == 0);
static_assert((kSlotBOffset % FLASH_SECTOR_SIZE) == 0);
static_assert(kSlotBOffset + kSlotSize <= kLegacyWideAreaStart);
static_assert(sizeof(oag::DiamondConfigRecord) <= kSlotSize);

extern "C" std::uint8_t __flash_binary_end;

struct LegacyDiamondComboStepV4 {
    bool enabled = false;
    oag::DiamondComboStepKind kind = oag::DiamondComboStepKind::Press;
    oag::DiamondLogicalControl control = oag::DiamondLogicalControl::None;
    std::uint16_t durationMs = 50;
    std::uint16_t intervalMs = 50;
    std::uint16_t repeatCount = 1;
};

struct LegacyDiamondComboProgramV4 {
    bool enabled = false;
    std::array<oag::DiamondComboTrigger, oag::kDiamondComboTriggers> triggers {};
    oag::DiamondComboActivationMode activation =
        oag::DiamondComboActivationMode::WhileHeld;
    oag::DiamondComboRepeatMode repeat =
        oag::DiamondComboRepeatMode::Once;
    bool passTriggerThrough = true;
    bool cancelOnTriggerRelease = true;
    bool cancelOnTriggerPressAgain = false;
    bool cancelControlEnabled = false;
    oag::DiamondLogicalControl cancelControl =
        oag::DiamondLogicalControl::None;
    std::uint8_t stepCount = 0;
    std::array<LegacyDiamondComboStepV4, oag::kDiamondComboSteps> steps {};
};

struct LegacyContentNamesV4 {
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondGameSlots> games {};
    std::array<
        std::array<
            std::array<char, oag::kDiamondDisplayNameBytes>,
            oag::kDiamondWeaponSlotsPerGame
        >,
        oag::kDiamondGameSlots
    > weapons {};
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondComboSlots> combos {};
    std::array<oag::DiamondComboTiming, oag::kDiamondComboSlots> comboTiming {};
    std::array<LegacyDiamondComboProgramV4, oag::kDiamondComboSlots> comboPrograms {};
};

struct LegacyPersistentConfigV4 {
    static constexpr std::uint32_t kMagic = 0x4F414750u;
    static constexpr std::uint16_t kSchemaVersion = 4;
    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    oag::DiamondRuntimeConfig runtime {};
    oag::DiamondSecurityConfig security {};
    LegacyContentNamesV4 names {};
};

struct LegacyConfigRecordV4 {
    static constexpr std::uint32_t kMagic = 0x4F414743u;
    static constexpr std::uint16_t kRecordVersion = 4;
    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t payloadLength = sizeof(LegacyPersistentConfigV4);
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    LegacyPersistentConfigV4 payload {};
};

static_assert(sizeof(LegacyConfigRecordV4) <= kSlotSize);

struct LegacyContentNamesV3 {
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondGameSlots> games {};
    std::array<
        std::array<
            std::array<char, oag::kDiamondDisplayNameBytes>,
            oag::kDiamondWeaponSlotsPerGame
        >,
        oag::kDiamondGameSlots
    > weapons {};
    std::array<std::array<char, oag::kDiamondDisplayNameBytes>, oag::kDiamondComboSlots> combos {};
    std::array<oag::DiamondComboTiming, oag::kDiamondComboSlots> comboTiming {};
};

struct LegacyPersistentConfigV3 {
    static constexpr std::uint32_t kMagic = 0x4F414750u;
    static constexpr std::uint16_t kSchemaVersion = 3;
    std::uint32_t magic = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved = 0;
    oag::DiamondRuntimeConfig runtime {};
    oag::DiamondSecurityConfig security {};
    LegacyContentNamesV3 names {};
};

struct LegacyConfigRecordV3 {
    static constexpr std::uint32_t kMagic = 0x4F414743u;
    static constexpr std::uint16_t kRecordVersion = 3;
    std::uint32_t magic = kMagic;
    std::uint16_t recordVersion = kRecordVersion;
    std::uint16_t payloadLength = sizeof(LegacyPersistentConfigV3);
    std::uint32_t generation = 0;
    std::uint32_t payloadCrc32 = 0;
    LegacyPersistentConfigV3 payload {};
};

static_assert(sizeof(LegacyConfigRecordV3) <= kSlotSize);

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

const LegacyConfigRecordV4* legacyV4RecordAt(std::uint32_t offset) {
    return reinterpret_cast<const LegacyConfigRecordV4*>(XIP_BASE + offset);
}

bool validLegacyV4(const LegacyConfigRecordV4& record) {
    return
        record.magic == LegacyConfigRecordV4::kMagic &&
        record.recordVersion == LegacyConfigRecordV4::kRecordVersion &&
        record.payloadLength == sizeof(LegacyPersistentConfigV4) &&
        record.payload.magic == LegacyPersistentConfigV4::kMagic &&
        record.payload.schemaVersion == LegacyPersistentConfigV4::kSchemaVersion &&
        record.payload.runtime.magic == oag::DiamondRuntimeConfig::kMagic &&
        record.payload.runtime.schemaVersion == oag::DiamondRuntimeConfig::kSchemaVersion &&
        record.payloadCrc32 == oag::diamondConfigCrc32(
            &record.payload, sizeof(record.payload)
        );
}

const LegacyConfigRecordV4* selectLegacyV4() {
    const auto* a = legacyV4RecordAt(kLegacyWideSlotAOffset);
    const auto* b = legacyV4RecordAt(kLegacyWideSlotBOffset);
    const bool va = validLegacyV4(*a);
    const bool vb = validLegacyV4(*b);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;
    const auto delta = static_cast<std::int32_t>(b->generation - a->generation);
    return delta > 0 ? b : a;
}

const LegacyConfigRecordV3* legacyV3RecordAt(std::uint32_t offset) {
    return reinterpret_cast<const LegacyConfigRecordV3*>(XIP_BASE + offset);
}

bool validLegacyV3(const LegacyConfigRecordV3& record) {
    return
        record.magic == LegacyConfigRecordV3::kMagic &&
        record.recordVersion == LegacyConfigRecordV3::kRecordVersion &&
        record.payloadLength == sizeof(LegacyPersistentConfigV3) &&
        record.payload.magic == LegacyPersistentConfigV3::kMagic &&
        record.payload.schemaVersion == LegacyPersistentConfigV3::kSchemaVersion &&
        record.payload.runtime.magic == oag::DiamondRuntimeConfig::kMagic &&
        record.payload.runtime.schemaVersion == oag::DiamondRuntimeConfig::kSchemaVersion &&
        record.payloadCrc32 == oag::diamondConfigCrc32(
            &record.payload, sizeof(record.payload)
        );
}

const LegacyConfigRecordV3* selectLegacyV3() {
    const auto* a = legacyV3RecordAt(kLegacyWideSlotAOffset);
    const auto* b = legacyV3RecordAt(kLegacyWideSlotBOffset);
    const bool va = validLegacyV3(*a);
    const bool vb = validLegacyV3(*b);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;
    const auto delta = static_cast<std::int32_t>(b->generation - a->generation);
    return delta > 0 ? b : a;
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
    const auto* a = legacyV2RecordAt(kLegacyWideSlotAOffset);
    const auto* b = legacyV2RecordAt(kLegacyWideSlotBOffset);
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

    // Transparent V4 -> V5 migration. Preserve every existing OAG
    // Game/Weapon/Combo and convert each legacy single-control step into the
    // new multi-control chord representation.
    if (const auto* legacyV4 = selectLegacyV4(); legacyV4 != nullptr) {
        config_.runtime = legacyV4->payload.runtime;
        config_.security = legacyV4->payload.security;
        config_.names.games = legacyV4->payload.names.games;
        config_.names.weapons = legacyV4->payload.names.weapons;
        config_.names.combos = legacyV4->payload.names.combos;
        config_.names.comboTiming = legacyV4->payload.names.comboTiming;

        for (std::size_t i = 0; i < oag::kDiamondComboSlots; ++i) {
            const auto& oldProgram = legacyV4->payload.names.comboPrograms[i];
            auto& program = config_.names.comboPrograms[i];
            program.enabled = oldProgram.enabled;
            program.triggers = oldProgram.triggers;
            program.activation = oldProgram.activation;
            program.repeat = oldProgram.repeat;
            program.passTriggerThrough = oldProgram.passTriggerThrough;
            program.cancelOnTriggerRelease = oldProgram.cancelOnTriggerRelease;
            program.cancelOnTriggerPressAgain = oldProgram.cancelOnTriggerPressAgain;
            program.cancelControlEnabled = oldProgram.cancelControlEnabled;
            program.cancelControl = oldProgram.cancelControl;
            program.stepCount = oldProgram.stepCount;

            for (std::size_t s = 0; s < oldProgram.steps.size(); ++s) {
                const auto& oldStep = oldProgram.steps[s];
                auto& step = program.steps[s];
                step.enabled = oldStep.enabled;
                step.kind = oldStep.kind;
                step.control = oldStep.control;
                step.durationMs = oldStep.durationMs;
                step.intervalMs = oldStep.intervalMs;
                step.repeatCount = oldStep.repeatCount;
                const auto raw = static_cast<std::uint8_t>(oldStep.control);
                if (raw > 0 && raw < 32) {
                    step.logicalMask = 1u << raw;
                }
            }
        }

        generation_ = legacyV4->generation;
        activeSlot_ = legacyV4 == legacyV4RecordAt(kLegacyWideSlotAOffset) ? 0u : 1u;
        loadedFromFlash_ = true;
        return true;
    }

    // Transparent V3 -> V4 migration. Preserve runtime settings, security,
    // user-created OAG names and the first-generation combo timing records.
    // Programmable combo steps start empty until the user creates/edits them.
    if (const auto* legacyV3 = selectLegacyV3(); legacyV3 != nullptr) {
        config_.runtime = legacyV3->payload.runtime;
        config_.security = legacyV3->payload.security;
        config_.names.games = legacyV3->payload.names.games;
        config_.names.weapons = legacyV3->payload.names.weapons;
        config_.names.combos = legacyV3->payload.names.combos;
        config_.names.comboTiming = legacyV3->payload.names.comboTiming;
        generation_ = legacyV3->generation;
        activeSlot_ = legacyV3 == legacyV3RecordAt(kLegacyWideSlotAOffset) ? 0u : 1u;
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
        activeSlot_ = legacyV2 == legacyV2RecordAt(kLegacyWideSlotAOffset) ? 0u : 1u;
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
