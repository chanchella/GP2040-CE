#include <cassert>
#include <climits>
#include <cstring>
#include <cmath>
#include "oag/mapping/pro_input_processor.h"
#include "oag/config/diamond_config_record.h"
using namespace oag;
int main() {
    ProInputSettings s; assert(validProInputSettings(s)); assert(s.targetDpi==1600&&s.processingHz==1000&&s.fractionalRemainder);
    assert(proDpiGainQ16(s)==65536);
    s.sourceDpi=800; s.dpiSource=ProDpiSource::Manual; assert(proDpiGainQ16(s)==131072);
    s.sourceDpi=3200; assert(proDpiGainQ16(s)==32768);
    ProInputProcessor p; const DeviceId a{0,1},b{1,1};
    assert(p.processMouse(a,{1,1},s).dx==0); assert(p.processMouse(b,{1,1},s).dx==0);
    assert(p.processMouse(a,{1,1},s).dx==1); assert(p.processMouse(b,{-1,-1},s).dx==0);
    assert(p.processMouse({0,2},{1,1},s).dx==0); assert(p.stats(0).remainderX==32768);
    p.resetFractions(); assert(p.processMouse(a,{-1,-1},s).dx==0); assert(p.processMouse(a,{-1,-1},s).dx==-1);
    s.fractionalRemainder=false; assert(p.processMouse(a,{1,1},s).dx==0); assert(p.processMouse(a,{1,1},s).dx==0);
    s={}; s.automaticMultiplier=false;s.multiplierPermille=300;
    std::int64_t total=0; for(int i=0;i<10000;++i)total+=p.processMouse(a,{1,0},s).dx;
    assert(total>=2999&&total<=3001); p.resetFractions();
    std::int64_t negative=0;for(int i=0;i<10000;++i)negative+=p.processMouse(a,{-1,0},s).dx;assert(negative==-total);
    s.multiplierPermille=32000;s.gainXPermille=s.gainYPermille=8000;
    auto saturated=p.processMouse(a,{INT32_MAX,INT32_MIN},s);assert(saturated.dx==INT32_MAX&&saturated.dy==INT32_MIN);
    LogicalGamepadState raw;raw.connected=true;raw.buttons=43;raw.rx=INT32_MAX;raw.ry=INT32_MIN;raw.lx=10;raw.ly=-10;s={};
    auto mapped=p.processGamepad(raw,s);assert(mapped.rx==raw.rx&&mapped.ry==raw.ry&&mapped.lx==10&&mapped.buttons==43);
    s.innerDeadzonePermille=50;s.outerDeadzonePermille=20;mapped=p.processGamepad(raw,s);assert(mapped.lx==0&&mapped.ly==0&&mapped.rx==INT32_MAX&&mapped.ry==INT32_MIN);
    s={};s.curvePermille=2000;raw.rx=INT32_MAX/2;mapped=p.processGamepad(raw,s);assert(mapped.rx<raw.rx&&mapped.rx>0);
    for(int i=0;i<2000;++i)assert(p.processGamepad(raw,s).rx==mapped.rx);
    ProInputConfig c;DeviceRecord d;d.vid=0x046d;d.pid=0xc077;int slot=proProfileSlot(c,d,ProInputKind::Mouse);assert(slot==0);
    auto& profile=c.devices[slot];profile.enabled=true;profile.kind=ProInputKind::Mouse;profile.vid=d.vid;profile.pid=d.pid;profile.settings.sourceDpi=800;profile.settings.dpiSource=ProDpiSource::Manual;
    assert(proSettingsFor(c,d,ProInputKind::Mouse).sourceDpi==800);assert(proSettingsFor(c,d,ProInputKind::Gamepad).sourceDpi==0);
    d.transport=TransportType::BluetoothLe;assert(proSettingsFor(c,d,ProInputKind::Mouse).sourceDpi==0);
    s={};s.processingHz=8000;assert(!validProInputSettings(s));s={};s.dpiSource=ProDpiSource::Manual;assert(!validProInputSettings(s));
    ProProcessingClock clock;assert(clock.due(0,1000));assert(!clock.due(999,1000));assert(clock.due(1000,1000));assert(clock.due(1000000,1000));assert(!clock.due(1000001,1000));clock.reset();assert(clock.due(0,125));assert(!clock.due(7999,125));assert(clock.due(8000,125));
    for(int i=0;i<=500;++i)p.noteReport(b,1000+i*1000);assert(p.stats(1).reportHz>=1000&&p.stats(1).reportHz<=1002);
    p.noteProcessTime(b,8);p.noteProcessTime(b,3);assert(p.stats(1).lastProcessUs==3&&p.stats(1).maxProcessUs==8);
    DiamondConfigRecordV5 old;old.payload.runtime.activeGame=7;old.payload.runtime.activeWeapon=4;std::strcpy(old.payload.names.games[0].data(),"my game");old.payload.names.comboPrograms[0].steps[0].logicalMask=1u<<25;old.payloadCrc32=diamondConfigCrc32(&old.payload,sizeof(old.payload));
    DiamondPersistentConfig migrated;assert(migrateDiamondConfigV5(old,migrated));assert(migrated.runtime.activeGame==7&&migrated.runtime.activeWeapon==4);assert(migrated.names.comboPrograms[0].steps[0].logicalMask==(1u<<25));assert(!std::strcmp(migrated.names.games[0].data(),"my game"));assert(migrated.proInput.defaults[0].processingHz==1000);
    ++old.payloadCrc32;assert(!migrateDiamondConfigV5(old,migrated));assert(migrated.runtime.activeGame==7);
    DiamondConfigRecord record;record.payload=migrated;finalizeDiamondConfigRecord(record);assert(validateDiamondConfigRecord(record));record.payload.proInput.defaults[0].processingHz=8000;finalizeDiamondConfigRecord(record);assert(!validateDiamondConfigRecord(record));
}
