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

constexpr std::uint32_t kSlotSize = FLASH_SECTOR_SIZE;
// BTstack owns two sectors near the end of flash (and RP2350 may reserve
// the final sector for errata handling). Keep Diamond config immediately
// before BTstack's official storage region so pairing data is never touched.
constexpr std::uint32_t kSlotBOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - kSlotSize;
constexpr std::uint32_t kSlotAOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - (2u * kSlotSize);

static_assert(PICO_FLASH_BANK_STORAGE_OFFSET >= (2u * kSlotSize));
static_assert((kSlotAOffset % FLASH_SECTOR_SIZE) == 0);
static_assert((kSlotBOffset % FLASH_SECTOR_SIZE) == 0);
static_assert(kSlotBOffset + kSlotSize <= PICO_FLASH_BANK_STORAGE_OFFSET);
static_assert(sizeof(oag::DiamondConfigRecord) <= FLASH_SECTOR_SIZE);

extern "C" std::uint8_t __flash_binary_end;

struct FlashWriteContext {
    std::uint32_t offset = 0;
    const std::uint8_t* data = nullptr;
};

void __not_in_flash_func(writeConfigSector)(void* raw) {
    const auto* context = static_cast<const FlashWriteContext*>(raw);
    flash_range_erase(context->offset, FLASH_SECTOR_SIZE);
    flash_range_program(
        context->offset,
        context->data,
        FLASH_SECTOR_SIZE
    );
}

const oag::DiamondConfigRecord* recordAt(std::uint32_t offset) {
    return reinterpret_cast<const oag::DiamondConfigRecord*>(
        XIP_BASE + offset
    );
}

bool firmwareLeavesConfigAreaFree() {
    const auto binaryEnd =
        reinterpret_cast<std::uintptr_t>(&__flash_binary_end);
    return binaryEnd <= (XIP_BASE + kSlotAOffset);
}

} // namespace

namespace oag::firmware {

bool DiamondConfigStore::load() {
    config_ = oag::DiamondPersistentConfig {};
    generation_ = 0;
    activeSlot_ = 0xFF;
    loadedFromFlash_ = false;

    if (!firmwareLeavesConfigAreaFree()) {
        return false;
    }

    const auto* slotA = recordAt(kSlotAOffset);
    const auto* slotB = recordAt(kSlotBOffset);
    const auto* selected =
        oag::selectNewestDiamondConfigRecord(slotA, slotB);

    if (selected == nullptr) {
        return true;
    }

    config_ = selected->payload;
    generation_ = selected->generation;
    activeSlot_ = selected == slotA ? 0u : 1u;
    loadedFromFlash_ = true;
    return true;
}

bool DiamondConfigStore::save() {
    if (!firmwareLeavesConfigAreaFree()) {
        return false;
    }

    pendingRecord_ = oag::DiamondConfigRecord {};
    pendingRecord_.generation = generation_ + 1u;
    pendingRecord_.payload = config_;
    oag::finalizeDiamondConfigRecord(pendingRecord_);

    const std::uint8_t targetSlot = activeSlot_ == 0u ? 1u : 0u;
    const std::uint32_t targetOffset =
        targetSlot == 0u ? kSlotAOffset : kSlotBOffset;

    alignas(FLASH_PAGE_SIZE) static std::uint8_t sector[FLASH_SECTOR_SIZE];
    std::memset(sector, 0xFF, sizeof(sector));
    std::memcpy(sector, &pendingRecord_, sizeof(pendingRecord_));

    FlashWriteContext context {
        targetOffset,
        sector,
    };

    const int result = flash_safe_execute(
        writeConfigSector,
        &context,
        1000
    );
    if (result != PICO_OK) {
        return false;
    }

    const auto* written = recordAt(targetOffset);
    if (!oag::validateDiamondConfigRecord(*written)) {
        return false;
    }

    generation_ = pendingRecord_.generation;
    activeSlot_ = targetSlot;
    loadedFromFlash_ = true;
    return true;
}

} // namespace oag::firmware
