#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <memory>
#include "oag/config/diamond_config_record.h"
#include "oag/config/oag_smart_api.h"
using namespace oag;
struct Memory final:OagSmartStorage {
    std::array<std::vector<std::uint8_t>,40> banks;unsigned writes=0;bool fail=false;
    Memory(){for(auto& b:banks)b.resize(32768,255);}
    const OagSmartRecord* record(std::size_t g,std::uint8_t c) const override {return reinterpret_cast<const OagSmartRecord*>(banks[g*2+c].data());}
    bool ready() const override {return true;}
    bool write(std::size_t g,std::uint8_t c,const std::uint8_t* b,std::size_t size) override {++writes;std::memcpy(banks[g*2+c].data(),b,fail?100:size);return !fail;}
};
OagSmartCombo make() {
    OagSmartCombo c{};std::strcpy(c.name.data(),"OAG test");c.enabled=1;auto& b=c.branches[0];
    b.conditions[0].control={OagSource::Keyboard,0,4};b.conditions[0].kind=OagTrigger::Held;
    b.thenCount=1;b.actions[0].control={OagSource::Gamepad,0,4};return c;
}
int main() {
    Memory memory;OagSmartStore store(memory);assert(store.activate(0));auto c=make();
    assert(store.beginCombo(0,0,c,7));assert(store.branch(0,c.branches[0],7));assert(!store.applyCombo(8));
    assert(store.applyCombo(7));assert(memory.writes==0);const auto comboRevision=store.comboRevision();
    OagWeaponSettings w{};w.configured=w.enabled=1;w.vertical=75;assert(store.previewWeapon(0,0,w));assert(store.comboRevision()==comboRevision);
    assert(store.saveCombo(0,0,7));assert(memory.writes==1);
    OagSmartStore reboot(memory);assert(reboot.activate(0));assert(reboot.active().combos[0].enabled);assert(!reboot.active().weapons[0].configured);
    assert(store.saveWeapon(0,0));assert(reboot.activate(0));assert(reboot.active().weapons[0].vertical==75);
    assert(reboot.activate(19));assert(!reboot.active().combos[0].enabled);assert(!reboot.active().weapons[0].configured);assert(reboot.activate(0));assert(reboot.active().combos[0].enabled);
    const auto oldWrites=memory.writes;c.branches[0].actions[0].durationMs=321;
    assert(store.beginCombo(0,0,c,9));assert(store.branch(0,c.branches[0],9));assert(store.applyCombo(9));
    memory.fail=true;assert(!store.saveCombo(0,0,9));memory.fail=false;OagSmartStore recover(memory);assert(recover.activate(0));
    assert(recover.active().combos[0].branches[0].actions[0].durationMs==50);assert(memory.writes==oldWrites+1);
    c.branches[0].actions[0].durationMs=65535;assert(!oagValidateCombo(c));c=make();c.branches[0].actions[0].kind=OagActionKind::Repeat;assert(!oagValidateCombo(c));
    OagSmartApi api(store);assert(api.handle("GET","/api/oag/status",""));assert(api.status()==200);
    assert(api.handle("POST","/api/oag/combo-begin","game=1&slot=1&token=22&name=OAG&enabled=1&mode=0&cancelable=1&branches=1"));assert(api.status()==202);
    assert(api.handle("GET","/api/oag/combo?game=2&slot=2",""));assert(api.status()==409);api.task();assert(store.draftMatches(0,0,22));
    assert(api.handle("POST","/api/oag/combo-apply","game=2&slot=1&token=22"));api.task();
    assert(api.handle("GET","/api/oag/status",""));assert(std::strstr(api.response(),"\"ok\":false"));const auto before=memory.writes;
    assert(api.handle("POST","/api/oag/weapon-save","game=1&game=2&slot=1"));assert(api.status()==400);
    assert(api.handle("GET","/api/oag/combo?game=21&slot=1",""));assert(api.status()==400);
    assert(api.handle("POST","/api/oag/test","game=1&slot=1&branch=0"));assert(api.status()==400);assert(memory.writes==before);
    auto preview=make();preview.branches[0].actions[0].durationMs=555;
    assert(store.beginCombo(0,0,preview,30));assert(store.branch(0,preview.branches[0],30));assert(store.applyCombo(30));
    assert(store.beginCombo(0,0,preview,31));OagSmartCombo current;
    assert(store.combo(0,0,current));assert(current.branches[0].actions[0].durationMs==555);
    assert(!store.applyCombo(31));store.discardCombo();
    assert(store.active().combos[0].branches[0].actions[0].durationMs==50);assert(memory.writes==before);
    Memory migrated;
    auto old=std::make_unique<OagSmartGameV1>();auto original=make();std::memcpy(&old->combos[0],&original,sizeof(OagSmartComboV1));old->weapons[4].configured=1;old->weapons[4].vertical=-123;
    auto* header=reinterpret_cast<OagSmartRecord*>(migrated.banks[0].data());header->magic=OagSmartRecord::kMagic;header->version=1;header->game=0;header->generation=42;
    std::memcpy(migrated.banks[0].data()+16,old.get(),sizeof(*old));header->crc=diamondConfigCrc32(old.get(),sizeof(*old));
    OagSmartStore fromV1(migrated);assert(fromV1.activate(0));assert(migrated.writes==0);assert(fromV1.active().combos[0].enabled);assert(!fromV1.active().combos[0].cancel.enabled);assert(fromV1.active().weapons[4].vertical==-123);
    OagSmartCombo read;assert(fromV1.combo(0,0,read,true));assert(read.branches[0].actions[0].flags==0);OagWeaponSettings wr;assert(fromV1.weapon(0,4,wr,true)&&wr.vertical==-123);
    wr.horizontal=77;assert(fromV1.previewWeapon(0,4,wr));assert(fromV1.saveWeapon(0,4));assert(migrated.writes==1);assert(migrated.record(0,1)->version==2);assert(migrated.record(0,0)->version==1);
    OagSmartStore afterMigration(migrated);assert(afterMigration.activate(0));assert(afterMigration.active().weapons[4].horizontal==77);assert(afterMigration.active().combos[0].enabled);
    auto withCancel=make();withCancel.cancel.enabled=1;withCancel.cancel.conditionCount=1;withCancel.cancel.conditions[0].control={OagSource::Keyboard,0,41};withCancel.cancel.conditions[0].kind=OagTrigger::Held;
    assert(afterMigration.beginCombo(0,0,withCancel,99));assert(afterMigration.branch(0,withCancel.branches[0],99));assert(afterMigration.applyCombo(99));assert(afterMigration.saveCombo(0,0,99));
    assert(afterMigration.activate(0));assert(afterMigration.active().combos[0].cancel.enabled);assert(afterMigration.active().weapons[4].horizontal==77);
    std::cout<<"OAG Smart storage/API regression scenarios PASS\n";
}
