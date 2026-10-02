#include <cassert>
#include <iostream>
#include "oag/mapping/oag_weapon_tuning_engine.h"
using namespace oag;
int main(){
    OagWeaponSettings w{};w.configured=w.enabled=1;w.vertical=100;w.horizontal=-50;assert(oagValidateWeapon(w));assert(OagWeaponTuningEngine::shotInterval(w)==100);
    OagWeaponTuningEngine e;auto t=e.tick(w,true,false,false,1000);assert(t.updated&&t.xQ16==-50*65536&&t.yQ16==100*65536);
    assert(!e.tick(w,true,false,false,2000).updated);LogicalGamepadState original{};original.rx=123;original.ry=456;original.leftTrigger=55;
    auto corrected=e.apply(original);assert(corrected.rx<original.rx&&corrected.ry>original.ry&&corrected.leftTrigger==55);
    e.tick(w,false,false,false,3000);assert(e.apply(original).rx==original.rx);
    w.startDelayMs=100;w.firstShot=1;w.firstVertical=20;w.adsOverride=1;w.adsVertical=60;e.reset();
    assert(!e.tick(w,true,true,false,0).updated);t=e.tick(w,true,true,false,100000);assert(t.yQ16==80*65536);
    assert(e.tick(w,true,true,false,140000).yQ16==60*65536);
    w.firstShot=0;w.startDelayMs=0;w.rampMs=1000;w.curve=OagRecoilCurve::Linear;e.reset();assert(e.tick(w,true,false,false,0).yQ16==0);
    assert(e.tick(w,true,false,false,500000).yQ16==50*65536);
    w.curve=OagRecoilCurve::EaseIn;e.reset();e.tick(w,true,false,false,0);assert(e.tick(w,true,false,false,500000).yQ16==25*65536);
    w.curve=OagRecoilCurve::Stages;w.stageX={0,50,100};w.stageY={150,200,0};e.reset();e.tick(w,true,false,false,0);
    assert(e.tick(w,true,false,false,500000).yQ16==150*65536);
    w.curve=OagRecoilCurve::Direct;w.smoothing=100;e.reset();t=e.tick(w,true,false,false,0);assert(t.yQ16>0&&t.yQ16<100*65536);
    w.smoothing=0;w.syncRpm=1;assert(OagWeaponTuningEngine::tickInterval(w)==100);
    e.reset();e.tick(w,true,false,true,0);assert(!e.active());e.tick(w,true,false,false,1000000);assert(!e.active());assert(e.tick(w,true,false,false,2500000).updated);
    w.firingMode=OagFiringMode::Single;e.reset();assert(e.tick(w,true,false,false,0).yQ16);assert(e.tick(w,true,false,false,100000).yQ16==0);
    w.firingMode=OagFiringMode::Burst;w.burstCount=3;e.reset();e.tick(w,true,false,false,0);assert(e.tick(w,true,false,false,200000).yQ16);assert(e.tick(w,true,false,false,300000).yQ16==0);
    w.vertical=201;assert(!oagValidateWeapon(w));w.vertical=0;w.stageMs[1]=w.stageMs[0];assert(!oagValidateWeapon(w));
    std::cout<<"OAG Weapon Tuning regression scenarios PASS\n";
}
