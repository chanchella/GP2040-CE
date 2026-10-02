#pragma once
#include "oag/config/oag_smart_config.h"

namespace oag {
class OagSmartStorage {
public:
    virtual ~OagSmartStorage() = default;
    virtual const OagSmartRecord* record(std::size_t game, std::uint8_t copy) const = 0;
    virtual bool write(std::size_t game, std::uint8_t copy, const std::uint8_t* bytes,
                       std::size_t size) = 0;
    virtual bool ready() const = 0;
};
class OagSmartStore {
public:
    explicit OagSmartStore(OagSmartStorage& storage) : storage_(storage) {}
    bool activate(std::size_t game);
    std::size_t activeGame() const { return activeGame_; }
    const OagSmartGame& active() const { return active_; }
    std::uint32_t revision() const { return revision_; }
    std::uint32_t comboRevision() const { return comboRevision_; }
    std::uint32_t weaponRevision() const { return weaponRevision_; }
    bool ready() const { return storage_.ready(); }
    bool combo(std::size_t game, std::size_t slot, OagSmartCombo& out, bool saved=false) const;
    bool weapon(std::size_t game, std::size_t slot, OagWeaponSettings& out, bool saved=false) const;
    bool beginCombo(std::size_t game, std::size_t slot, const OagSmartCombo& metadata, std::uint32_t token);
    bool branch(std::size_t index, const OagBranch& value, std::uint32_t token);
    bool applyCombo(std::uint32_t token);
    bool saveCombo(std::size_t game, std::size_t slot, std::uint32_t token);
    bool draftMatches(std::size_t game,std::size_t slot,std::uint32_t token) const {
        return (comboEditing_ || comboApplied_) && game==comboGame_ && slot==comboSlot_ && token==token_;
    }
    bool previewWeapon(std::size_t game, std::size_t slot, const OagWeaponSettings&);
    bool saveWeapon(std::size_t game, std::size_t slot);
    void discardCombo();
    void discardWeapon();
    void requestTest(std::size_t slot, std::size_t branch) { testSlot_=std::uint8_t(slot); testBranch_=std::uint8_t(branch); }
    bool takeTest(std::uint8_t& slot, std::uint8_t& branch);
    void requestCancel() { cancelRequested_=true; }
    bool takeCancel() { const bool c=cancelRequested_; cancelRequested_=false; return c; }
    static bool valid(const OagSmartRecord&, std::size_t game);
    std::uint32_t flashWrites() const { return flashWrites_; }
private:
    const OagSmartRecord* newest(std::size_t game) const;
    bool save(std::size_t game, std::size_t slot, bool weapon);
    OagSmartStorage& storage_;
    OagSmartGame active_ {};
    OagSmartCombo draftCombo_ {};
    OagWeaponSettings draftWeapon_ {};
    std::size_t activeGame_=0, comboGame_=0, comboSlot_=0, weaponGame_=0, weaponSlot_=0;
    std::uint32_t token_=0, revision_=1, flashWrites_=0;
    std::uint32_t comboRevision_=1, weaponRevision_=1;
    std::uint8_t branchMask_=0, testSlot_=255, testBranch_=0;
    bool comboEditing_=false, comboApplied_=false, weaponDirty_=false, cancelRequested_=false;
};
} // namespace oag
