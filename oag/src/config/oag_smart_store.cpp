#include "oag/config/oag_smart_store.h"
#include "oag/config/diamond_config_record.h"
#include <cstring>
#include <new>

namespace oag {
namespace {
const OagSmartGameV1& legacy(const OagSmartRecord* r) {
    return *reinterpret_cast<const OagSmartGameV1*>(reinterpret_cast<const std::uint8_t*>(r)+offsetof(OagSmartRecord,payload));
}
}
void OagSmartStore::readCombo(const OagSmartRecord* r,std::size_t slot,OagSmartCombo& out) {
    if (!r) { new(&out) OagSmartCombo {}; return; }
    if (r->version==1) oagMigrateCombo(legacy(r).combos[slot],out);
    else out=r->payload.combos[slot];
}
OagWeaponSettings OagSmartStore::readWeapon(const OagSmartRecord* r,std::size_t slot) {
    return !r?OagWeaponSettings{}:r->version==1?legacy(r).weapons[slot]:r->payload.weapons[slot];
}
void OagSmartStore::readGame(const OagSmartRecord* r,OagSmartGame& out) {
    for (std::size_t i=0;i<out.combos.size();++i) readCombo(r,i,out.combos[i]);
    for (std::size_t i=0;i<out.weapons.size();++i) out.weapons[i]=readWeapon(r,i);
}
bool OagSmartStore::valid(const OagSmartRecord& r, std::size_t game) {
    if (r.magic!=OagSmartRecord::kMagic || (r.version!=1 && r.version!=OagSmartRecord::kVersion) || r.game!=game) return false;
    if (r.version==1) {
        const auto& old=legacy(&r);
        if (r.crc!=diamondConfigCrc32(&old,sizeof(old))) return false;
        for (const auto& c:old.combos) if (!oagValidateComboV1(c)) return false;
        for (const auto& w:old.weapons) if (!oagValidateWeapon(w)) return false;
    } else {
        if (r.crc!=diamondConfigCrc32(&r.payload,sizeof(r.payload))) return false;
        for (const auto& c:r.payload.combos) if (!oagValidateCombo(c)) return false;
        for (const auto& w:r.payload.weapons) if (!oagValidateWeapon(w)) return false;
    }
    return true;
}
const OagSmartRecord* OagSmartStore::newest(std::size_t game) const {
    if (game>=kDiamondLibraryGameSlots || !storage_.ready()) return nullptr;
    const auto* a=storage_.record(game,0); const auto* b=storage_.record(game,1);
    const bool va=a && valid(*a,game), vb=b && valid(*b,game);
    if (!va) return vb?b:nullptr;
    if (!vb) return a;
    return std::int32_t(b->generation-a->generation)>0?b:a;
}
bool OagSmartStore::activate(std::size_t game) {
    if (game>=kDiamondLibraryGameSlots) return false;
    const auto* r=newest(game);
    readGame(r,active_);
    activeGame_=game;
    if (comboApplied_ && comboGame_==game) active_.combos[comboSlot_]=draftCombo_;
    if (weaponDirty_ && weaponGame_==game) active_.weapons[weaponSlot_]=draftWeapon_;
    ++revision_; ++comboRevision_; ++weaponRevision_; return true;
}
bool OagSmartStore::combo(std::size_t game,std::size_t slot,OagSmartCombo& out,bool saved) const {
    if (game>=kDiamondLibraryGameSlots || slot>=kDiamondComboSlots) return false;
    if (!saved && comboApplied_ && game==comboGame_ && slot==comboSlot_) { out=draftCombo_; return true; }
    if (!saved && comboEditing_ && game==activeGame_ && game==comboGame_ && slot==comboSlot_) {
        out=active_.combos[slot]; return true;
    }
    const auto* r=newest(game);
    readCombo(r,slot,out);
    return true;
}
bool OagSmartStore::weapon(std::size_t game,std::size_t slot,OagWeaponSettings& out,bool saved) const {
    if (game>=kDiamondLibraryGameSlots || slot>=kDiamondWeaponSlotsPerGame) return false;
    if (!saved && weaponDirty_ && game==weaponGame_ && slot==weaponSlot_) { out=draftWeapon_; return true; }
    const auto* r=newest(game); out=readWeapon(r,slot); return true;
}
bool OagSmartStore::beginCombo(std::size_t game,std::size_t slot,const OagSmartCombo& m,std::uint32_t token) {
    if (game>=kDiamondLibraryGameSlots || slot>=kDiamondComboSlots || !token ||
        m.enabled>1 || m.cancelable>1 || unsigned(m.mode)>5 || m.branchCount<1 ||
        m.branchCount>kOagSmartBranches || !std::memchr(m.name.data(),0,m.name.size()) || !oagValidateCancel(m.cancel)) return false;
    // An applied draft remains in active RAM until the complete replacement is validated.
    if ((comboApplied_ || comboEditing_) && comboGame_==activeGame_ && (game!=comboGame_ || slot!=comboSlot_)) {
        const auto* previous=newest(activeGame_);
        readCombo(previous,comboSlot_,active_.combos[comboSlot_]);
        ++comboRevision_;
    }
    draftCombo_=m; comboGame_=game; comboSlot_=slot; token_=token;
    branchMask_=0; comboEditing_=true; comboApplied_=false; return true;
}
bool OagSmartStore::branch(std::size_t index,const OagBranch& value,std::uint32_t token) {
    if (!comboEditing_ || token!=token_ || index>=draftCombo_.branchCount) return false;
    draftCombo_.branches[index]=value; branchMask_|=std::uint8_t(1u<<index); return true;
}
bool OagSmartStore::applyCombo(std::uint32_t token) {
    if (!comboEditing_ || token!=token_ ||
        branchMask_!=std::uint8_t((1u<<draftCombo_.branchCount)-1u) || !oagValidateCombo(draftCombo_)) return false;
    comboEditing_=false; comboApplied_=true;
    if (comboGame_==activeGame_) active_.combos[comboSlot_]=draftCombo_;
    ++revision_; ++comboRevision_; return true;
}
bool OagSmartStore::previewWeapon(std::size_t game,std::size_t slot,const OagWeaponSettings& w) {
    if (game>=kDiamondLibraryGameSlots || slot>=kDiamondWeaponSlotsPerGame || !oagValidateWeapon(w)) return false;
    // Restore a previous unsaved target before moving the one-weapon RAM preview.
    if (weaponDirty_ && weaponGame_==activeGame_ && (game!=weaponGame_ || slot!=weaponSlot_)) {
        const auto* r=newest(activeGame_);
        active_.weapons[weaponSlot_]=readWeapon(r,weaponSlot_);
    }
    draftWeapon_=w; weaponGame_=game; weaponSlot_=slot; weaponDirty_=true;
    if (game==activeGame_) active_.weapons[slot]=w;
    ++revision_; ++weaponRevision_; return true;
}
bool OagSmartStore::save(std::size_t game,std::size_t slot,bool weapon) {
    if (!storage_.ready()) return false;
    const auto* current=newest(game);
    const auto copy=std::uint8_t(current==storage_.record(game,0)?1:0);
    alignas(256) static std::array<std::uint8_t,32768> bytes {};
    bytes.fill(0xff);
    auto* next=new(bytes.data()) OagSmartRecord {};
    readGame(current,next->payload);
    next->game=std::uint16_t(game); next->generation=current?current->generation+1:1;
    if (weapon) next->payload.weapons[slot]=draftWeapon_;
    else next->payload.combos[slot]=draftCombo_;
    next->crc=diamondConfigCrc32(&next->payload,sizeof(next->payload));
    if (!storage_.write(game,copy,bytes.data(),bytes.size())) return false;
    const auto* written=storage_.record(game,copy);
    if (!written || !valid(*written,game) || written->generation!=next->generation) return false;
    ++flashWrites_; ++revision_; return true;
}
bool OagSmartStore::saveCombo(std::size_t game,std::size_t slot,std::uint32_t token) {
    if (!comboApplied_ || game!=comboGame_ || slot!=comboSlot_ || token!=token_) return false;
    if (!save(game,slot,false)) return false;
    comboApplied_=false; return true;
}
bool OagSmartStore::saveWeapon(std::size_t game,std::size_t slot) {
    if (!weaponDirty_ || game!=weaponGame_ || slot!=weaponSlot_) return false;
    if (!save(game,slot,true)) return false;
    weaponDirty_=false; return true;
}
void OagSmartStore::discardCombo() {
    if ((comboApplied_ || comboEditing_) && comboGame_==activeGame_) {
        const auto* r=newest(activeGame_);
        readCombo(r,comboSlot_,active_.combos[comboSlot_]);
    }
    comboEditing_=comboApplied_=false; ++revision_; ++comboRevision_;
}
void OagSmartStore::discardWeapon() {
    if (weaponDirty_ && weaponGame_==activeGame_) {
        const auto* r=newest(activeGame_); active_.weapons[weaponSlot_]=readWeapon(r,weaponSlot_);
    }
    weaponDirty_=false; ++revision_; ++weaponRevision_;
}
bool OagSmartStore::takeTest(std::uint8_t& slot,std::uint8_t& branch) {
    if (testSlot_==255) return false;
    slot=testSlot_; branch=testBranch_; testSlot_=255; return true;
}
} // namespace oag
