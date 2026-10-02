#include <cassert>
#include <iostream>
#include "oag/mapping/oag_smart_combo_engine.h"
#include "oag/input/gamepad_state.h"
using namespace oag;
using Programs=std::array<OagSmartCombo,kDiamondComboSlots>;
OagControl g(unsigned n) { return {OagSource::Gamepad,0,std::uint16_t(n)}; }
OagBranch branch(OagTrigger type,unsigned out=4) {
    OagBranch b{}; b.conditions[0].control=g(6); b.conditions[0].kind=type;
    b.thenCount=1; b.actions[0].control=g(out); b.actions[0].kind=OagActionKind::Tap; b.actions[0].durationMs=100; return b;
}
Programs single(OagTrigger type=OagTrigger::Held) {
    Programs p{}; p[0].enabled=1; p[0].branches[0]=branch(type); return p;
}
struct Fixture {
    Programs p{}; OagSmartComboEngine e;
    KeyboardState k{}; MouseState m{}; LogicalGamepadState pad{};
    Fixture() { pad.connected=true; k.connected=true; m.connected=true; }
    void load() { for(const auto& x:p) assert(oagValidateCombo(x)); e.configure(p); }
    void tick(std::uint64_t n,bool context=true) { e.tick({&k,&m,pad},n*1000,context); }
    void down(std::uint64_t n,std::uint64_t buttons=ButtonRightBumper) { pad.buttons=buttons; tick(n); }
    void up(std::uint64_t n) { pad.buttons=0; tick(n); }
};
void taps() {
    for(unsigned count=1;count<=3;++count) {
        Fixture f; f.p=single(); auto& c=f.p[0]; c.branchCount=4;
        c.branches[0]=branch(OagTrigger::Single,1);c.branches[1]=branch(OagTrigger::Double,2);
        c.branches[2]=branch(OagTrigger::Triple,3); c.branches[2].conditions[0].windowMs=700;
        c.branches[3]=branch(OagTrigger::Long,4); f.load(); f.tick(0);
        for(unsigned i=0;i<count;++i) { f.down(10+i*100); f.up(40+i*100); }
        f.tick(709); assert(f.e.executionCount()==0); f.tick(710);
        assert(f.e.executionCount()==1);assert(f.e.output().gamepad.buttons==(1ull<<(count-1)));
        f.tick(900); assert(!f.e.active()); assert(f.e.executionCount()==1);
    }
    Fixture f; f.p=single(); f.p[0].branchCount=2;
    f.p[0].branches[0]=branch(OagTrigger::Single,1);f.p[0].branches[1]=branch(OagTrigger::Long,4);
    f.load();f.down(1);f.tick(501);assert(!f.e.active());f.tick(800);assert(!f.e.active());
    f.tick(801);assert(f.e.output().gamepad.buttons==ButtonNorth);f.up(850);f.tick(1400);assert(f.e.executionCount()==1);
    Fixture multi;multi.p=single(OagTrigger::Multi);multi.p[0].branches[0].conditions[0].taps=5;
    multi.load();for(unsigned i=0;i<5;++i){multi.down(1+i*50);multi.up(11+i*50);}multi.tick(501);assert(multi.e.executionCount()==1);
    Fixture differentWindows;differentWindows.p=single(OagTrigger::Double);differentWindows.p[0].branchCount=2;
    differentWindows.p[0].branches[1]=branch(OagTrigger::Triple);differentWindows.p[0].branches[1].conditions[0].windowMs=700;
    differentWindows.load();differentWindows.down(1);differentWindows.up(30);differentWindows.down(601);differentWindows.up(630);differentWindows.tick(701);
    assert(differentWindows.e.executionCount()==0); // Two taps 600 ms apart must not satisfy a 500 ms double.
}
void chordAndSequence() {
    Fixture f;f.p=single(OagTrigger::Single);f.p[1]=f.p[0];auto& b=f.p[1].branches[0];
    b.conditions[0].kind=OagTrigger::Chord;b.conditions[0].refCount=b.refCount=2;b.refs[0]=g(6);b.refs[1]=g(5);
    b.conditions[0].windowMs=80;b.actions[0].control=g(2);f.load();f.down(1);f.down(30,ButtonRightBumper|ButtonLeftBumper);
    assert(f.e.lastSlot()==1&&f.e.executionCount()==1);f.up(60);f.tick(700);assert(f.e.executionCount()==1);
    Fixture seq;seq.p=single(OagTrigger::Single);seq.p[1]=seq.p[0];auto& r=seq.p[1].branches[0];
    r.conditions[0].kind=OagTrigger::Sequence;r.conditions[0].windowMs=900;r.conditions[0].refCount=r.refCount=3;
    r.refs[0]=g(6);r.refs[1]=g(6);r.refs[2]=g(4);r.actions[0].control=g(3);seq.load();
    seq.down(1);seq.up(40);seq.down(100);seq.up(140);seq.down(250,ButtonNorth);
    assert(seq.e.lastSlot()==1&&seq.e.executionCount()==1);seq.up(300);seq.tick(1000);assert(seq.e.executionCount()==1);
    Fixture timeout;timeout.p=seq.p;timeout.load();timeout.down(1);timeout.up(10);timeout.tick(901);assert(timeout.e.lastSlot()==0);
}
void logic() {
    Fixture f;f.p=single(OagTrigger::Double);auto& b=f.p[0].branches[0];b.conditionCount=2;
    b.conditions[1].control=g(5);b.conditions[1].kind=OagTrigger::Held;f.load();
    f.down(1,ButtonLeftBumper|ButtonRightBumper);f.down(40,ButtonLeftBumper);f.down(100,ButtonLeftBumper|ButtonRightBumper);f.down(140,ButtonLeftBumper);f.tick(501);assert(f.e.executionCount()==1);
    Fixture duration;duration.p=single(OagTrigger::Double);auto& db=duration.p[0].branches[0];db.conditions[0].control=g(4);db.conditionCount=2;
    db.conditions[1].control=g(5);db.conditions[1].kind=OagTrigger::Hold;db.conditions[1].holdMs=800;duration.load();duration.down(1,ButtonLeftBumper);
    duration.down(850,ButtonLeftBumper|ButtonNorth);duration.down(880,ButtonLeftBumper);duration.down(950,ButtonLeftBumper|ButtonNorth);duration.down(980,ButtonLeftBumper);duration.tick(1350);assert(duration.e.executionCount()==1);
    duration.down(1700,ButtonLeftBumper|ButtonNorth);duration.down(1730,ButtonLeftBumper);duration.down(1800,ButtonLeftBumper|ButtonNorth);duration.down(1830,ButtonLeftBumper);duration.tick(2200);assert(duration.e.executionCount()==2);
    Fixture holdFirst;holdFirst.p=duration.p;std::swap(holdFirst.p[0].branches[0].conditions[0],holdFirst.p[0].branches[0].conditions[1]);
    holdFirst.load();holdFirst.down(1,ButtonLeftBumper);
    holdFirst.down(850,ButtonLeftBumper|ButtonNorth);holdFirst.down(880,ButtonLeftBumper);holdFirst.down(950,ButtonLeftBumper|ButtonNorth);holdFirst.down(980,ButtonLeftBumper);holdFirst.tick(1350);
    holdFirst.down(1700,ButtonLeftBumper|ButtonNorth);holdFirst.down(1730,ButtonLeftBumper);holdFirst.down(1800,ButtonLeftBumper|ButtonNorth);holdFirst.down(1830,ButtonLeftBumper);holdFirst.tick(2200);assert(holdFirst.e.executionCount()==2);
    Fixture noDuplicate;noDuplicate.p=single(OagTrigger::Long);noDuplicate.p[0].branchCount=2;
    noDuplicate.p[0].branches[1]=branch(OagTrigger::Hold);noDuplicate.p[0].branches[1].conditions[0].holdMs=1200;
    noDuplicate.load();noDuplicate.down(1);noDuplicate.tick(801);noDuplicate.tick(1201);assert(noDuplicate.e.executionCount()==1);
    Fixture negative;negative.p=f.p;negative.p[0].branches[0].conditions[1].negate=1;negative.load();
    negative.down(1);negative.up(30);negative.down(100);negative.up(130);negative.tick(501);assert(negative.e.executionCount()==1);
    Fixture fallback;fallback.p=f.p;auto& r=fallback.p[0].branches[0];r.elseCount=1;r.actions[1]=r.actions[0];r.actions[1].control=g(2);fallback.load();
    fallback.down(1);fallback.up(10);fallback.down(60);fallback.up(70);fallback.tick(501);assert(fallback.e.output().gamepad.buttons==ButtonEast);
    Fixture either;either.p=single();auto& orB=either.p[0].branches[0];orB.conditionCount=2;
    orB.conditions[1].control=g(5);orB.conditions[1].kind=OagTrigger::Held;orB.conditions[1].join=OagJoin::Or;either.load();either.down(1,ButtonLeftBumper);assert(either.e.executionCount()==1);
    Fixture falseTerm;falseTerm.p=single();auto& ft=falseTerm.p[0].branches[0];ft.conditionCount=3;
    ft.conditions[1].control=g(5);ft.conditions[1].kind=OagTrigger::Held;
    ft.conditions[2].control=g(4);ft.conditions[2].kind=OagTrigger::Held;ft.conditions[2].join=OagJoin::Or;
    falseTerm.load();falseTerm.down(1,ButtonNorth);falseTerm.tick(151);falseTerm.down(160,ButtonNorth|ButtonRightBumper);
    assert(falseTerm.e.executionCount()==1); // A pulse in a false AND term must not refire the true OR term.
    Fixture independent;independent.p=single(OagTrigger::Double);auto& ib=independent.p[0].branches[0];ib.conditionCount=2;
    ib.conditions[1].control=g(5);ib.conditions[1].kind=OagTrigger::Held;ib.conditions[1].join=OagJoin::Or;
    independent.p[1]=single(OagTrigger::Single)[0];independent.p[1].branches[0].actions[0].control=g(2);
    independent.load();independent.down(1);independent.up(20);independent.down(50,ButtonLeftBumper);independent.tick(501);
    assert(independent.e.executionCount()==2);assert(independent.e.lastSlot()==1); // L1's OR term does not consume a separate R1 gesture.
}
void nativeAnalogAndCancellation() {
    Fixture f;f.p=single();auto& b=f.p[0].branches[0];b.conditions[0].control={OagSource::Keyboard,0,224};b.thenCount=4;
    b.actions[0].control={OagSource::Keyboard,0,0x1a};b.actions[0].kind=OagActionKind::Press;
    b.actions[1].control={OagSource::Mouse,0,2};b.actions[1].kind=OagActionKind::Press;
    b.actions[2].control={OagSource::Axis,0,0};b.actions[2].kind=OagActionKind::StickValue;b.actions[2].x=300;b.actions[2].y=-400;b.actions[2].durationMs=300;
    b.actions[3].control=g(7);b.actions[3].kind=OagActionKind::TriggerValue;b.actions[3].x=500;
    f.load();f.k.modifiers=1;f.tick(1);assert(f.e.output().keyboard.pressed(0x1a));assert(f.e.output().mouse.buttons==MouseButtonMiddle);
    const auto original=f.pad;f.e.cancel();assert(f.e.merge(original).buttons==original.buttons);assert(!f.e.output().keyboard.pressed(0x1a)&&!f.e.output().mouse.buttons);
    Fixture axis;axis.p=single(OagTrigger::Threshold);auto& r=axis.p[0].branches[0];r.conditions[0].control={OagSource::Axis,0,5};r.conditions[0].threshold=300;
    r.actions[0].control={OagSource::Stick,0,12};r.actions[0].kind=OagActionKind::StickDirection;r.actions[0].x=500;axis.load();axis.pad.rightTrigger=30000;axis.tick(1);
    assert(axis.e.output().gamepad.rx>0&&axis.e.output().gamepad.ry<0);axis.tick(101);assert(!axis.e.active());
    Fixture analog;analog.p=single(OagTrigger::Analog);analog.p[0].branches[0].conditions[0].control={OagSource::Stick,0,4};
    analog.load();analog.pad.lx=1600000000;analog.pad.ly=-1600000000;analog.tick(1);assert(analog.e.active());analog.tick(2,false);assert(!analog.e.active());
    Fixture wheel;wheel.p=single(OagTrigger::Single);wheel.p[0].branches[0].conditions[0].control={OagSource::Wheel,0,1};
    wheel.load();wheel.m.wheel=1;wheel.m.timestampUs=1;wheel.tick(1);wheel.tick(501);assert(wheel.e.executionCount()==1);wheel.tick(1001);assert(wheel.e.executionCount()==1);
    Fixture release;release.p=single(OagTrigger::Release);release.load();release.down(1);release.up(20);assert(release.e.executionCount()==1);
}
void modesAndTiming() {
    Fixture f;f.p=single();f.p[0].mode=OagExecution::Toggle;f.p[0].branches[0].actions[0].kind=OagActionKind::Press;
    f.load();f.down(1);assert(f.e.active());f.up(2);f.down(3);assert(!f.e.active());
    Fixture whileHeld;whileHeld.p=f.p;whileHeld.p[0].mode=OagExecution::WhileHeld;whileHeld.load();whileHeld.down(1);assert(whileHeld.e.active());whileHeld.up(2);assert(!whileHeld.e.active());
    // A held modifier is a live qualifier, including after PRESS has latched.
    for (auto mode:{OagExecution::WhileHeld,OagExecution::RepeatWhileHeld,OagExecution::StopOnRelease}) {
        Fixture qualified;qualified.p=single();qualified.p[0].mode=mode;
        auto& b=qualified.p[0].branches[0];b.conditionCount=2;b.conditions[1].control=g(5);b.conditions[1].kind=OagTrigger::Held;
        if(mode==OagExecution::WhileHeld)b.actions[0].kind=OagActionKind::Press;
        qualified.load();qualified.down(1,ButtonRightBumper|ButtonLeftBumper);assert(qualified.e.active());
        qualified.down(2,ButtonRightBumper);assert(!qualified.e.active());assert(!qualified.e.output().gamepad.buttons);
    }
    Fixture heldChord;heldChord.p=single(OagTrigger::Chord);heldChord.p[0].mode=OagExecution::WhileHeld;
    auto& chord=heldChord.p[0].branches[0];chord.refCount=chord.conditions[0].refCount=2;chord.refs[0]=g(6);chord.refs[1]=g(5);
    chord.actions[0].kind=OagActionKind::Press;heldChord.load();heldChord.down(1,ButtonRightBumper|ButtonLeftBumper);assert(heldChord.e.active());heldChord.down(2,ButtonRightBumper);assert(!heldChord.e.active());
    Fixture timing;timing.p=single();auto& r=timing.p[0].branches[0];r.actions[0].beforeMs=100;r.actions[0].afterMs=80;r.actions[0].durationMs=250;
    timing.load();timing.down(1);assert(!timing.e.output().gamepad.buttons);timing.tick(100);assert(!timing.e.output().gamepad.buttons);timing.tick(101);assert(timing.e.output().gamepad.buttons);
    timing.tick(351);assert(!timing.e.output().gamepad.buttons);timing.tick(431);assert(!timing.e.active());
    Fixture repeat;repeat.p=single();auto& rr=repeat.p[0].branches[0];rr.thenCount=2;rr.actions[0].durationMs=10;rr.actions[0].afterMs=0;
    rr.actions[1].kind=OagActionKind::Repeat;rr.actions[1].first=0;rr.actions[1].count=3;rr.actions[1].intervalMs=10;repeat.load();
    repeat.down(1);repeat.tick(11);repeat.tick(21);repeat.tick(31);repeat.tick(41);repeat.tick(51);assert(!repeat.e.active());
    Fixture loop;loop.p=repeat.p;loop.p[0].branches[0].actions[1].kind=OagActionKind::Loop;loop.load();loop.down(1);loop.tick(11);loop.tick(21);assert(loop.e.active());loop.tick(22,false);assert(!loop.e.active());
    Fixture disabled;disabled.p=single();disabled.p[0].enabled=0;disabled.load();disabled.e.test(0,0,1000);disabled.tick(1);assert(disabled.e.active());disabled.tick(10001);assert(!disabled.e.active());
}
void actionStrengths() {
    Fixture f;f.p=single();auto& b=f.p[0].branches[0];b.thenCount=3;
    b.refCount=2;b.refs[0]=g(1);b.refs[1]={OagSource::Keyboard,0,4};
    b.actions[0].kind=OagActionKind::MultiPress;b.actions[0].first=0;b.actions[0].count=2;b.actions[0].durationMs=20;
    b.actions[1].kind=OagActionKind::TriggerValue;b.actions[1].control=g(8);b.actions[1].x=400;b.actions[1].durationMs=30;
    b.actions[2].kind=OagActionKind::Pulse;b.actions[2].control=g(3);b.actions[2].count=3;b.actions[2].durationMs=10;b.actions[2].intervalMs=5;
    f.load();f.down(1);assert(f.e.output().gamepad.buttons==ButtonSouth);assert(f.e.output().keyboard.pressed(4));
    f.tick(21);assert(!f.e.output().keyboard.pressed(4));assert(f.e.output().gamepad.rightTrigger==26214);
    auto physical=f.pad;physical.rightTrigger=50000;assert(f.e.merge(physical).rightTrigger==50000);
    f.tick(51);assert(f.e.output().gamepad.buttons==ButtonWest);f.tick(61);assert(!f.e.output().gamepad.buttons);
    f.tick(66);assert(f.e.output().gamepad.buttons==ButtonWest);f.tick(76);f.tick(81);assert(f.e.output().gamepad.buttons==ButtonWest);
    f.tick(91);f.tick(96);assert(!f.e.active());assert(!f.e.output().gamepad.rightTrigger);
}
int main() {taps();chordAndSequence();logic();nativeAnalogAndCancellation();modesAndTiming();actionStrengths();std::cout<<"OAG Smart logic regression scenarios PASS\n";}
