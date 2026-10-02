#include "oag/firmware/smart_combo_store.h"
#include <array>
#include <cstring>
#include <new>
#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "pico/platform.h"

namespace {
// Same end calculation as the frozen library, followed by a separate region
// below it. Nothing changes the legacy game/config/BT bank offsets or sizes.
constexpr std::uint32_t legacyGameStart = PICO_FLASH_BANK_STORAGE_OFFSET -
    2u * 4u * FLASH_SECTOR_SIZE - 2u * 6u * FLASH_SECTOR_SIZE - 20u * 2u * 4u * FLASH_SECTOR_SIZE;
constexpr std::uint32_t smartBytes = oag::kOagSmartGames * 2u * oag::kOagSmartSlotBytes;
static_assert(legacyGameStart >= smartBytes, "OAG smart storage does not fit this board");
constexpr std::uint32_t smartStart = legacyGameStart - smartBytes;
static_assert(smartStart % FLASH_SECTOR_SIZE == 0);
static_assert(oag::kOagSmartSlotBytes % FLASH_SECTOR_SIZE == 0);
extern "C" std::uint8_t __flash_binary_end;
const oag::OagSmartRecord* at(std::size_t game, unsigned copy) {
    return reinterpret_cast<const oag::OagSmartRecord*>(XIP_BASE + smartStart +
        game * 2u * oag::kOagSmartSlotBytes + copy * oag::kOagSmartSlotBytes);
}
const oag::OagSmartRecord* newest(std::size_t game) {
    const auto* a = at(game, 0); const auto* b = at(game, 1);
    const bool va = oag::oagSmartRecordValid(*a, game), vb = oag::oagSmartRecordValid(*b, game);
    if (!va) return vb ? b : nullptr;
    if (!vb) return a;
    return static_cast<std::int32_t>(b->generation - a->generation) > 0 ? b : a;
}
struct Write { std::uint32_t offset; const std::uint8_t* bytes; };
void __not_in_flash_func(writeSmart)(void* p) {
    const auto& w = *static_cast<const Write*>(p);
    flash_range_erase(w.offset, oag::kOagSmartSlotBytes);
    constexpr std::size_t full = sizeof(oag::OagSmartRecord) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
    flash_range_program(w.offset, w.bytes, full);
    // Only the final page needs padding. Avoid a 57 KiB second save buffer.
    std::array<std::uint8_t, FLASH_PAGE_SIZE> tail;
    tail.fill(0xff);
    std::memcpy(tail.data(), w.bytes + full, sizeof(oag::OagSmartRecord) - full);
    flash_range_program(w.offset + full, tail.data(), tail.size());
}
}
namespace oag::firmware {
bool OagSmartComboStore::empty(std::size_t slot) const {
    if (slot >= kOagSmartCombos) return false;
    const auto& p = record_.payload.programs[slot];
    if (p.name[0] || p.enabled || p.branchCount != 1 || p.consumeInput) return false;
    const auto& b = p.branches[0];
    if (!b.enabled || b.otherwise || b.mode != OagSmartMode::Once || !b.cancelable || b.priority || b.conditionCount != 1 || b.actionCount != 1) return false;
    const auto& c = b.conditions[0]; const auto& a = b.actions[0];
    OagSmartCondition dc; OagSmartAction da;
    const auto blankTarget = [](const OagSmartTarget& t, unsigned code) {
        return t.kind == OagSmartTargetKind::Pad && (!t.code || t.code == code) && t.strength == 100;
    };
    // Both the fresh firmware template and the UI's deleted-slot template
    // are empty. Unnamed disabled custom definitions are still occupied.
    return c.trigger == dc.trigger && c.join == dc.join && !c.negate && c.targetCount == 1 && c.taps == dc.taps &&
        c.windowMs == dc.windowMs && c.holdMs == dc.holdMs && c.sequenceMs == dc.sequenceMs && c.chordMs == dc.chordMs &&
        c.thresholdPermille == dc.thresholdPermille && blankTarget(c.targets[0], 6) &&
        a.kind == da.kind && a.targetCount == 1 && !a.loopFrom && !a.lifetime && a.durationMs == da.durationMs &&
        a.releaseMs == da.releaseMs && !a.beforeMs && !a.afterMs && a.repeatCount == da.repeatCount && !a.intervalMs &&
        !a.valueX && !a.valueY && blankTarget(a.targets[0], 4);
}
bool OagSmartComboStore::ready() const {
    return reinterpret_cast<std::uintptr_t>(&__flash_binary_end) <= XIP_BASE + smartStart;
}
bool OagSmartComboStore::load(std::size_t game, OagSmartGame& out) const {
    if (game >= kOagSmartGames || !ready()) return false;
    const auto* record = newest(game);
    if (record) out = record->payload;
    else {
        // Construct in place, avoiding a 50 KiB automatic temporary on the
        // Pico's 4 KiB stack. Blank records are never written at boot.
        out.~OagSmartGame(); new (&out) OagSmartGame;
    }
    return true;
}
bool OagSmartComboStore::activate(std::size_t game) {
    if (game >= kOagSmartGames) return false;
    if (activeGame_ == game) return true;
    if (dirty_ || !load(game, record_.payload)) return false;
    activeGame_ = game; editorGame_ = kOagSmartGames; ++revision_; return true;
}
bool OagSmartComboStore::openEditor(std::size_t game) {
    if (game == editorGame_ && game == activeGame_) return true;
    if (dirty_) return false;
    if (!activate(game)) return false;
    editorGame_ = game; return true;
}
bool OagSmartComboStore::discard(std::size_t game) {
    if (dirty_ && editorGame_ != game) return false;
    if (!load(game, record_.payload)) return false;
    editorGame_ = activeGame_ = game; dirty_ = false; ++revision_;
    return true;
}
bool OagSmartComboStore::preview(std::size_t game, std::size_t slot, const OagSmartProgram& p) {
    const char* error = nullptr;
    if (slot >= kOagSmartCombos || !oagSmartValidate(p, error) || !openEditor(game)) return false;
    record_.payload.programs[slot] = p; dirty_ = true;
    ++revision_; return true;
}
bool OagSmartComboStore::save(std::size_t game) {
    if (!ready() || game != editorGame_) return false;
    if (!dirty_) return true;
    const auto* current = newest(game);
    const unsigned copy = current == at(game, 0) ? 1 : 0;
    auto* pending = &record_;
    pending->game = static_cast<std::uint16_t>(game);
    pending->generation = current ? current->generation + 1 : 1;
    pending->crc = oagSmartCrc(&pending->payload, sizeof(pending->payload));
    Write write {static_cast<std::uint32_t>(smartStart + game * 2u * kOagSmartSlotBytes + copy * kOagSmartSlotBytes), reinterpret_cast<const std::uint8_t*>(pending)};
    if (flash_safe_execute(writeSmart, &write, 2500) != PICO_OK || !oagSmartRecordValid(*at(game, copy), game)) return false;
    dirty_ = false; return true;
}
} // namespace oag::firmware
