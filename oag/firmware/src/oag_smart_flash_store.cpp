#include "oag/firmware/oag_smart_flash_store.h"
#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "pico/platform.h"

namespace {
// Exact FIX2 geometry. New area is BELOW the existing game library; rollback
// firmware continues reading every old config/game/bond address unchanged.
constexpr std::uint32_t kOldStart=PICO_FLASH_BANK_STORAGE_OFFSET-(8u+12u+160u)*FLASH_SECTOR_SIZE;
constexpr std::uint32_t kSlot=32768;
constexpr std::uint32_t kStart=kOldStart-20u*2u*kSlot;
static_assert(kStart%FLASH_SECTOR_SIZE==0);
static_assert(sizeof(oag::OagSmartRecord)<=kSlot);
extern "C" std::uint8_t __flash_binary_end;
struct Write { std::uint32_t offset; const std::uint8_t* data; };
void __not_in_flash_func(writeSlot)(void* p) {
    const auto& w=*static_cast<Write*>(p);
    flash_range_erase(w.offset,kSlot);
    flash_range_program(w.offset,w.data,kSlot);
}
}
namespace oag::firmware {
bool OagSmartFlashStorage::ready() const {
    return reinterpret_cast<std::uintptr_t>(&__flash_binary_end)<=XIP_BASE+kStart;
}
const oag::OagSmartRecord* OagSmartFlashStorage::record(std::size_t game,std::uint8_t copy) const {
    if (game>=20 || copy>1 || !ready()) return nullptr;
    return reinterpret_cast<const oag::OagSmartRecord*>(XIP_BASE+kStart+(game*2+copy)*kSlot);
}
bool OagSmartFlashStorage::write(std::size_t game,std::uint8_t copy,const std::uint8_t* bytes,std::size_t size) {
    if (game>=20 || copy>1 || !bytes || size!=kSlot || !ready()) return false;
    Write w{std::uint32_t(kStart+(game*2+copy)*kSlot),bytes};
    return flash_safe_execute(writeSlot,&w,2500)==PICO_OK;
}
} // namespace oag::firmware
