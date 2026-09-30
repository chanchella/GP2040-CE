#include "oag/firmware/diamond_game_library_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "pico/platform.h"

#include "oag/config/diamond_config_record.h"

namespace {

constexpr std::uint32_t kCoreSlotSize = 6u * FLASH_SECTOR_SIZE;
constexpr std::uint32_t kLegacyWideSlotSize = 4u * FLASH_SECTOR_SIZE;
constexpr std::uint32_t kLegacyWideAreaStart =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * kLegacyWideSlotSize);
constexpr std::uint32_t kCoreSlotAOffset =
    kLegacyWideAreaStart - (2u * kCoreSlotSize);

constexpr std::uint32_t kGameRecordSlotSize = 4u * FLASH_SECTOR_SIZE; // 16 KiB
constexpr std::uint32_t kGameRecordPairSize = 2u * kGameRecordSlotSize;
constexpr std::uint32_t kGameLibraryBytes =
    static_cast<std::uint32_t>(oag::kDiamondLibraryGameSlots) *
    kGameRecordPairSize;
constexpr std::uint32_t kGameLibraryEnd = kCoreSlotAOffset;
constexpr std::uint32_t kGameLibraryStart =
    kGameLibraryEnd - kGameLibraryBytes;

static_assert(kGameLibraryBytes == 655360u);
static_assert((kGameLibraryStart % FLASH_SECTOR_SIZE) == 0);
static_assert(sizeof(oag::DiamondGameRecord) <= kGameRecordSlotSize);

extern "C" std::uint8_t __flash_binary_end;

struct FlashWriteContext {
    std::uint32_t offset = 0;
    const std::uint8_t* data = nullptr;
};

void __not_in_flash_func(writeGameSlot)(void* raw) {
    const auto* context = static_cast<const FlashWriteContext*>(raw);
    flash_range_erase(context->offset, kGameRecordSlotSize);
    flash_range_program(
        context->offset,
        context->data,
        kGameRecordSlotSize
    );
}

std::uint32_t slotOffset(std::size_t gameIndex, std::uint8_t copy) {
    return
        kGameLibraryStart +
        static_cast<std::uint32_t>(gameIndex) * kGameRecordPairSize +
        static_cast<std::uint32_t>(copy) * kGameRecordSlotSize;
}

const oag::DiamondGameRecord* recordAt(
    std::size_t gameIndex,
    std::uint8_t copy
) {
    return reinterpret_cast<const oag::DiamondGameRecord*>(
        XIP_BASE + slotOffset(gameIndex, copy)
    );
}

bool validRecord(
    const oag::DiamondGameRecord& record,
    std::size_t gameIndex
) {
    return
        record.magic == oag::DiamondGameRecord::kMagic &&
        record.recordVersion == oag::DiamondGameRecord::kRecordVersion &&
        record.gameIndex == gameIndex &&
        record.payloadCrc32 == oag::diamondConfigCrc32(
            &record.payload,
            sizeof(record.payload)
        );
}

const oag::DiamondGameRecord* newestRecord(std::size_t gameIndex) {
    const auto* a = recordAt(gameIndex, 0);
    const auto* b = recordAt(gameIndex, 1);
    const bool va = validRecord(*a, gameIndex);
    const bool vb = validRecord(*b, gameIndex);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;

    const auto delta =
        static_cast<std::int32_t>(b->generation - a->generation);
    return delta > 0 ? b : a;
}

bool firmwareLeavesLibraryFree() {
    const auto binaryEnd =
        reinterpret_cast<std::uintptr_t>(&__flash_binary_end);
    return binaryEnd <= (XIP_BASE + kGameLibraryStart);
}

bool hasText(const char* text) {
    return text != nullptr && text[0] != '\0';
}

void seedEfootballComboOne(oag::DiamondGameContent& game) {
    // Seed only a completely unused Game 1 slot. Once the user has named the
    // game or configured Combo 1, firmware upgrades never overwrite it.
    if (
        hasText(game.gameName.data()) ||
        hasText(game.comboNames[0].data()) ||
        game.comboPrograms[0].enabled
    ) {
        return;
    }

    constexpr char kGameName[] = "eFootball";
    constexpr char kComboName[] = "L2 Toggle + Cross Pulse";
    std::memcpy(game.gameName.data(), kGameName, sizeof(kGameName));
    std::memcpy(game.comboNames[0].data(), kComboName, sizeof(kComboName));

    auto& program = game.comboPrograms[0];
    program = oag::DiamondComboProgram {};
    program.enabled = true;
    program.triggers[0].enabled = true;
    program.triggers[0].kind =
        oag::DiamondComboTriggerKind::LogicalControl;
    program.triggers[0].code = static_cast<std::uint16_t>(
        oag::DiamondLogicalControl::LeftTrigger
    );
    program.activation = oag::DiamondComboActivationMode::Toggle;
    program.repeat = oag::DiamondComboRepeatMode::Once;
    program.passTriggerThrough = false;
    program.cancelOnTriggerRelease = false;
    program.cancelOnTriggerPressAgain = false;
    program.cancelControlEnabled = false;
    program.stepCount = 2;

    // Step 1: latch L2 until the Toggle program is stopped by the next
    // physical L2 press.
    auto& holdL2 = program.steps[0];
    holdL2.enabled = true;
    holdL2.kind = oag::DiamondComboStepKind::HoldStart;
    holdL2.control = oag::DiamondLogicalControl::LeftTrigger;
    holdL2.durationMs = 0;
    holdL2.intervalMs = 0;
    holdL2.repeatCount = 1;
    // Persist as HOLD UNTIL COMBO END so the editor and runtime both describe
    // the toggle contract accurately. Toggle-off releases the generated hold.
    holdL2.delayAfterMs = 0xFFFFu;

    // Step 2: PlayStation Cross / Xbox A logical South button.
    // 200 ms down, 750 ms released, forever until Combo 1 is toggled off.
    auto& pulseX = program.steps[1];
    pulseX.enabled = true;
    pulseX.kind = oag::DiamondComboStepKind::Pulse;
    pulseX.control = oag::DiamondLogicalControl::South;
    pulseX.durationMs = 200;
    pulseX.intervalMs = 750;
    pulseX.repeatCount = 0;
    pulseX.delayAfterMs = 0;

    auto& timing = game.comboTiming[0];
    timing.enabled = true;
    timing.pressMs = 200;
    timing.delayAfterMs = 750;
    timing.repeatCount = 0;
}

} // namespace

namespace oag::firmware {

bool DiamondGameLibraryStore::storageReady() const {
    return firmwareLeavesLibraryFree();
}

void DiamondGameLibraryStore::synthesizeLegacy(
    std::size_t gameIndex,
    const oag::DiamondPersistentConfig& legacy,
    bool includeLegacyCombos,
    oag::DiamondGameContent& out
) const {
    out = oag::DiamondGameContent {};

    if (gameIndex >= oag::kDiamondGameSlots) {
        return;
    }

    out.gameName = legacy.names.games[gameIndex];
    out.weaponNames = legacy.names.weapons[gameIndex];
    out.weapons = legacy.runtime.games[gameIndex].weapons;

    if (includeLegacyCombos) {
        out.comboNames = legacy.names.combos;
        out.comboTiming = legacy.names.comboTiming;
        out.comboPrograms = legacy.names.comboPrograms;
    }
}

bool DiamondGameLibraryStore::loadStored(
    std::size_t gameIndex,
    oag::DiamondGameContent& out
) const {
    if (
        gameIndex >= oag::kDiamondLibraryGameSlots ||
        !firmwareLeavesLibraryFree()
    ) {
        return false;
    }

    const auto* selected = newestRecord(gameIndex);
    if (selected == nullptr) {
        return false;
    }

    out = selected->payload;
    return true;
}

bool DiamondGameLibraryStore::loadGame(
    std::size_t gameIndex,
    const oag::DiamondPersistentConfig& legacy,
    oag::DiamondGameContent& out
) const {
    if (gameIndex >= oag::kDiamondLibraryGameSlots) {
        return false;
    }

    if (loadStored(gameIndex, out)) {
        return true;
    }

    synthesizeLegacy(gameIndex, legacy, false, out);
    return true;
}

bool DiamondGameLibraryStore::saveGame(
    std::size_t gameIndex,
    const oag::DiamondGameContent& game
) {
    if (
        gameIndex >= oag::kDiamondLibraryGameSlots ||
        !firmwareLeavesLibraryFree()
    ) {
        return false;
    }

    const auto* current = newestRecord(gameIndex);
    const std::uint32_t generation =
        current == nullptr ? 1u : current->generation + 1u;
    const std::uint8_t targetCopy =
        current == recordAt(gameIndex, 0) ? 1u : 0u;

    oag::DiamondGameRecord pending {};
    pending.gameIndex = static_cast<std::uint16_t>(gameIndex);
    pending.generation = generation;
    pending.payload = game;
    pending.payloadCrc32 = oag::diamondConfigCrc32(
        &pending.payload,
        sizeof(pending.payload)
    );

    alignas(FLASH_PAGE_SIZE)
        static std::array<std::uint8_t, kGameRecordSlotSize> slot {};
    slot.fill(0xFF);
    std::memcpy(slot.data(), &pending, sizeof(pending));

    FlashWriteContext context {
        slotOffset(gameIndex, targetCopy),
        slot.data(),
    };
    const int result =
        flash_safe_execute(writeGameSlot, &context, 2500);
    if (result != PICO_OK) {
        return false;
    }

    const auto* written = recordAt(gameIndex, targetCopy);
    if (!validRecord(*written, gameIndex)) {
        return false;
    }

    if (activeGame_ == gameIndex) {
        active_ = game;
    }
    return true;
}

bool DiamondGameLibraryStore::initialize(
    const oag::DiamondPersistentConfig& legacy
) {
    if (!firmwareLeavesLibraryFree()) {
        return false;
    }

    // Built-in starter content requested for Game 1. This also upgrades an
    // already-flashed V13 device whose Game 1 bank is still completely empty.
    oag::DiamondGameContent gameOne {};
    if (!loadStored(0, gameOne)) {
        synthesizeLegacy(0, legacy, false, gameOne);
    }
    const bool gameOneWasEmpty =
        !hasText(gameOne.gameName.data()) &&
        !hasText(gameOne.comboNames[0].data()) &&
        !gameOne.comboPrograms[0].enabled;
    if (gameOneWasEmpty) {
        seedEfootballComboOne(gameOne);
        if (!saveGame(0, gameOne)) {
            return false;
        }
    }

    const std::size_t requested =
        legacy.runtime.activeGame < oag::kDiamondLibraryGameSlots
            ? legacy.runtime.activeGame
            : 0u;

    activeGame_ = requested;

    if (loadStored(activeGame_, active_)) {
        return true;
    }

    // V12 and earlier had one global combo bank. Preserve that bank only for
    // the game that was active during the one-time migration. Other legacy
    // games retain their names/weapons/recoil and start with empty combos.
    synthesizeLegacy(activeGame_, legacy, true, active_);

    // Persist the migrated active game immediately so later game switches can
    // never reinterpret the old global combo bank as belonging to another game.
    return saveGame(activeGame_, active_);
}

bool DiamondGameLibraryStore::activate(
    std::size_t gameIndex,
    const oag::DiamondPersistentConfig& legacy
) {
    if (gameIndex >= oag::kDiamondLibraryGameSlots) {
        return false;
    }

    oag::DiamondGameContent next {};
    if (!loadGame(gameIndex, legacy, next)) {
        return false;
    }

    activeGame_ = gameIndex;
    active_ = next;
    return true;
}

} // namespace oag::firmware
