#include "oag/mapping/smart_combo_engine.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/smart_combo_legacy.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace oag;
namespace {
OagSmartTarget pad(unsigned c, unsigned strength = 100) { return {static_cast<std::uint16_t>(c), OagSmartTargetKind::Pad, static_cast<std::uint8_t>(strength)}; }
OagSmartTarget key(unsigned c) { return {static_cast<std::uint16_t>(c), OagSmartTargetKind::Key, 100}; }
OagSmartGame game;
OagSmartComboEngine engine;
OagSmartInput input;
std::uint64_t now = 0;
void clear() { game = {}; engine.reset(); input = {}; input.pad.connected = input.keyboard.connected = input.mouse.connected = true; now = 0; }
void tick(unsigned milliseconds) { now = std::uint64_t(milliseconds) * 1000; engine.tick(game, input, now); }
void button(unsigned c, bool on, unsigned at) {
    OagSmartOutput out; oagSmartSet(pad(c), true, out);
    if (c == 7) input.pad.leftTrigger = on ? 65535 : 0;
    else if (c == 8) input.pad.rightTrigger = on ? 65535 : 0;
    else if (on) input.pad.buttons |= out.pad.buttons; else input.pad.buttons &= ~out.pad.buttons;
    tick(at);
}
OagSmartBranch& branch(unsigned slot, OagSmartTrigger trigger, unsigned triggerButton, unsigned outputButton, unsigned branchIndex = 0) {
    auto& p = game.programs[slot]; p.enabled = 1; p.branchCount = std::max<unsigned>(p.branchCount, branchIndex + 1);
    auto& b = p.branches[branchIndex]; b = {}; b.conditions[0].trigger = trigger; b.conditions[0].targets[0] = pad(triggerButton);
    b.actions[0].targets[0] = pad(outputButton); b.actions[0].durationMs = 100; return b;
}
bool generated(unsigned c) { OagSmartInput i; i.pad = engine.output().pad; i.pad.connected = true; return oagSmartDown(pad(c), i, 1); }
void tapAt(unsigned start) { button(6, true, start); button(6, false, start + 20); }
void validation() {
    clear(); auto& b = branch(0, OagSmartTrigger::Single, 6, 1); const char* e = nullptr;
    assert(oagSmartValidate(game.programs[0], e)); b.conditions[0].negate = 1; assert(!oagSmartValidate(game.programs[0], e));
    b.conditions[0].negate = 0; b.actions[0].kind = OagSmartActionKind::Loop; b.actions[0].loopFrom = 0; assert(!oagSmartValidate(game.programs[0], e));
    b.actions[0].kind = OagSmartActionKind::Tap; b.actions[0].targets[0].code = 35; assert(!oagSmartValidate(game.programs[0], e));
    b.actions[0].targets[0] = pad(1); b.conditionCount = 2;
    b.conditions[1].trigger = OagSmartTrigger::Held; b.conditions[1].targets[0] = pad(5);
    b.conditions[1].join = OagSmartJoin::Or; b.conditions[1].negate = 1;
    assert(!oagSmartValidate(game.programs[0], e)); // An OR group needs a positive input.
    b.conditionCount = 3; b.conditions[1].negate = 0;
    b.conditions[2].trigger = OagSmartTrigger::Held; b.conditions[2].targets[0] = pad(8); b.conditions[2].negate = 1;
    assert(oagSmartValidate(game.programs[0], e));
    OagSmartRecord record; record.game = 2; record.crc = oagSmartCrc(&record.payload, sizeof(record.payload));
    assert(oagSmartRecordValid(record, 2)); assert(!oagSmartRecordValid(record, 1)); record.payload.programs[0].name[0] = 'x'; assert(!oagSmartRecordValid(record, 2));
}
void tapArbitration() {
    clear(); branch(0, OagSmartTrigger::Single, 6, 1).conditions[0].windowMs = 500;
    branch(1, OagSmartTrigger::Double, 6, 2).conditions[0].windowMs = 500;
    branch(2, OagSmartTrigger::Triple, 6, 3).conditions[0].windowMs = 700;
    tapAt(0); tick(499); assert(engine.lastSlot() == -1); tapAt(500); tick(700);
    assert(engine.lastSlot() == 1 && generated(2)); assert(!generated(1) && !generated(3));
    const auto starts = engine.starts(); tick(900); assert(engine.starts() == starts);
    clear(); branch(0, OagSmartTrigger::Single, 6, 1); branch(1, OagSmartTrigger::Double, 6, 2);
    branch(2, OagSmartTrigger::Triple, 6, 3).conditions[0].windowMs = 700;
    tapAt(0); tapAt(200); tapAt(400); tick(700); assert(engine.lastSlot() == 2 && generated(3));
    clear(); auto& b = branch(0, OagSmartTrigger::Multi, 6, 4); b.conditions[0].taps = 4; b.conditions[0].windowMs = 700;
    tapAt(0); tapAt(100); tapAt(200); tapAt(300); tick(700); assert(engine.lastSlot() == 0 && generated(4));
}
void longAndRelease() {
    clear(); branch(0, OagSmartTrigger::Single, 6, 1); branch(1, OagSmartTrigger::Long, 6, 2).conditions[0].holdMs = 800;
    button(6, true, 0); tick(799); assert(!generated(2)); tick(800); assert(generated(2));
    button(6, false, 900); tick(1600); assert(engine.lastSlot() == 1 && !generated(1));
    clear(); branch(0, OagSmartTrigger::Release, 6, 3); button(6, true, 0); assert(!generated(3)); button(6, false, 1); assert(generated(3));
}
void logicAndPriority() {
    clear(); branch(0, OagSmartTrigger::Double, 4, 1);
    auto& b = branch(1, OagSmartTrigger::Held, 5, 2); b.conditionCount = 2;
    b.conditions[1].trigger = OagSmartTrigger::Double; b.conditions[1].targets[0] = pad(4);
    button(5, true, 0); button(4, true, 10); button(4, false, 30); button(4, true, 200); button(4, false, 220); tick(510);
    assert(engine.lastSlot() == 1 && generated(2) && !generated(1));
    clear(); auto& n = branch(0, OagSmartTrigger::Held, 6, 1); n.conditionCount = 2;
    n.conditions[1].trigger = OagSmartTrigger::Held; n.conditions[1].targets[0] = pad(5); n.conditions[1].negate = 1;
    button(5, true, 0); button(6, true, 1); assert(!generated(1)); button(5, false, 2); assert(generated(1));
    clear(); auto& o = branch(0, OagSmartTrigger::Held, 5, 1); o.conditionCount = 3;
    o.conditions[1].trigger = OagSmartTrigger::Release; o.conditions[1].targets[0] = pad(6); o.conditions[1].join = OagSmartJoin::Or;
    o.conditions[2].trigger = OagSmartTrigger::Held; o.conditions[2].targets[0] = pad(4);
    button(5, true, 0); auto before = engine.starts(); button(6, true, 1); button(6, false, 2); assert(engine.starts() == before);
}
void chordSequence() {
    clear(); branch(0, OagSmartTrigger::Single, 6, 1);
    auto& b = branch(1, OagSmartTrigger::Chord, 5, 2); b.conditions[0].targetCount = 2; b.conditions[0].targets[1] = pad(6);
    button(5, true, 0); button(6, true, 40); assert(generated(2)); button(5, false, 70); button(6, false, 90); tick(600); assert(!generated(1));
    clear(); branch(0, OagSmartTrigger::Single, 6, 1);
    auto& seq = branch(1, OagSmartTrigger::Sequence, 6, 2); auto& c = seq.conditions[0]; c.targetCount = 3; c.targets[1] = pad(6); c.targets[2] = pad(4);
    tapAt(0); tapAt(100); button(4, true, 200); assert(generated(2)); button(4, false, 220); tick(1100); assert(!generated(1));
    clear(); auto& timeout = branch(0, OagSmartTrigger::Sequence, 6, 2).conditions[0]; timeout.targetCount = 2; timeout.targets[1] = pad(4); timeout.sequenceMs = 100;
    tapAt(0); tick(101); button(4, true, 102); assert(!generated(2));
}
void modeAndActions() {
    clear(); auto& b = branch(0, OagSmartTrigger::Held, 6, 1); b.mode = OagSmartMode::Toggle;
    b.actions[0].kind = OagSmartActionKind::Hold; b.actions[0].durationMs = 0;
    button(6, true, 0); assert(generated(1)); button(6, false, 10); assert(generated(1)); button(6, true, 20); assert(!generated(1));
    clear(); auto& h = branch(0, OagSmartTrigger::Held, 6, 1); h.mode = OagSmartMode::RepeatHeld;
    h.actions[0].durationMs = 10; h.actions[0].releaseMs = 10;
    button(6, true, 0); assert(generated(1)); tick(10); assert(!generated(1)); tick(20); tick(21); assert(generated(1)); button(6, false, 22); assert(!engine.active());
    clear(); auto& seq = branch(0, OagSmartTrigger::Held, 6, 1); seq.actionCount = 4;
    seq.actions[0].kind = OagSmartActionKind::Press; seq.actions[0].targets[0] = pad(5); seq.actions[0].durationMs = 0;
    seq.actions[1].kind = OagSmartActionKind::Wait; seq.actions[1].durationMs = 100;
    seq.actions[2].targets[0] = pad(4); seq.actions[2].durationMs = 50;
    seq.actions[3].kind = OagSmartActionKind::ReleaseAll; seq.actions[3].targetCount = 0;
    button(6, true, 0); assert(generated(5) && !generated(4)); tick(100); assert(generated(5) && generated(4)); tick(150); tick(200); assert(!generated(5));
    input.pad.buttons |= ButtonSouth; auto physical = input.pad; oagSmartCompose(engine.output(), physical); assert(physical.buttons & ButtonSouth);
}
void analogAndNative() {
    clear(); auto& b = branch(0, OagSmartTrigger::Threshold, 8, 1); b.conditions[0].thresholdPermille = 600;
    input.pad.rightTrigger = 30000; tick(0); assert(!generated(1)); input.pad.rightTrigger = 40000; tick(1); assert(generated(1));
    clear(); auto& n = branch(0, OagSmartTrigger::Held, 6, 1); n.actions[0].targetCount = 3;
    n.actions[0].targets[0] = key(4); n.actions[0].targets[1] = key(228); n.actions[0].targets[2] = {1, OagSmartTargetKind::Mouse, 100};
    button(6, true, 0); assert(engine.output().keyboard.pressed(4)); assert(engine.output().keyboard.modifiers == 16); assert(engine.output().mouse.buttons == 1);
    clear(); auto& stick = branch(0, OagSmartTrigger::Held, 6, 22); stick.actions[0].kind = OagSmartActionKind::StickDirection;
    button(6, true, 0); assert(engine.output().pad.rx > 0 && engine.output().pad.ry < 0); tick(100); assert(engine.output().axes == 0);
    clear(); for (unsigned c = 1; c <= 34; ++c) {
        OagSmartOutput out; oagSmartSet(pad(c), true, out); OagSmartInput in; in.pad = out.pad;
        assert(oagSmartDown(pad(c), in, 500)); oagSmartSet(pad(c), false, out); in.pad = out.pad; assert(!oagSmartDown(pad(c), in, 500));
    }
    OagSmartOutput full; oagSmartSet(pad(18),true,full); OagSmartInput fullInput;fullInput.pad=full.pad;
    assert(oagSmartDown(pad(18),fullInput,1000));
}
void resetIsolation() {
    clear(); auto& b = branch(0, OagSmartTrigger::Held, 6, 1); b.actions[0].kind = OagSmartActionKind::Hold; b.actions[0].durationMs = 0; b.mode = OagSmartMode::Toggle;
    button(6, true, 0); assert(generated(1)); engine.reset(); game = {}; tick(1); assert(!engine.active() && !generated(1));
    clear(); branch(0, OagSmartTrigger::Held, 6, 1); game.programs[0].consumeInput = 1;
    button(6, true, 0); assert(input.pad.buttons & ButtonRightBumper); assert(!(engine.filteredInput().pad.buttons & ButtonRightBumper));
}
void branchesElseLoopsAndWheel() {
    clear(); auto& b = branch(0, OagSmartTrigger::Long, 6, 1); b.conditions[0].holdMs = 800;
    auto& p = game.programs[0]; p.branchCount = 2; auto& e = p.branches[1]; e.otherwise = 1; e.conditionCount = 0; e.actions[0].targets[0] = pad(2);
    button(6,true,0); button(6,false,100); assert(engine.lastBranch()==1 && generated(2));
    clear(); branch(0,OagSmartTrigger::Single,6,1); auto& l=branch(0,OagSmartTrigger::Long,6,2,1); l.conditions[0].holdMs=800;
    auto& p2=game.programs[0]; p2.branchCount=3; p2.branches[2].otherwise=1;p2.branches[2].conditionCount=0;p2.branches[2].actions[0].targets[0]=pad(3);
    button(6,true,0);button(6,false,100);assert(!generated(3));tick(500);assert(generated(1)&&engine.lastBranch()==0);
    clear(); branch(0,OagSmartTrigger::Long,6,1);branch(0,OagSmartTrigger::Long,8,2,1);
    auto& fallback=game.programs[0];fallback.branchCount=3;fallback.branches[2].otherwise=1;fallback.branches[2].conditionCount=0;fallback.branches[2].actions[0].targets[0]=pad(3);
    branch(1,OagSmartTrigger::Release,8,4);button(8,true,0);button(8,false,100);assert(generated(4)&&!generated(3));
    clear(); auto& loop=branch(0,OagSmartTrigger::Held,6,1);loop.actionCount=2;loop.actions[0].durationMs=10;loop.actions[0].releaseMs=10;
    loop.actions[1].kind=OagSmartActionKind::Repeat;loop.actions[1].targetCount=0;loop.actions[1].repeatCount=3;
    button(6,true,0);tick(10);tick(20);tick(21);assert(generated(1));tick(31);tick(41);tick(42);assert(generated(1));tick(52);tick(62);assert(!engine.active());
    clear(); auto& wheel=branch(0,OagSmartTrigger::Double,6,1);wheel.conditions[0].targets[0]={1,OagSmartTargetKind::Wheel,100};
    input.mouse.wheel=1;input.mouse.generation=1;tick(0);tick(1);input.mouse.wheel=0;tick(2);input.mouse.wheel=1;input.mouse.generation=2;tick(100);input.mouse.wheel=0;tick(101);tick(500);assert(generated(1));
    clear();auto& wout=branch(0,OagSmartTrigger::Held,6,1);wout.mode=OagSmartMode::Toggle;wout.actions[0].kind=OagSmartActionKind::Press;wout.actions[0].durationMs=0;wout.actions[0].targets[0]={1,OagSmartTargetKind::Wheel,100};
    button(6,true,0);assert(engine.output().mouse.wheel==1);tick(1);assert(engine.output().mouse.wheel==0);
    clear(); auto& one=branch(0,OagSmartTrigger::Held,5,1);one.conditionCount=2;one.conditions[1].trigger=OagSmartTrigger::Release;one.conditions[1].targets[0]=pad(6);
    auto& two=branch(1,OagSmartTrigger::Held,5,2);two.conditionCount=2;two.conditions[1].trigger=OagSmartTrigger::Release;two.conditions[1].targets[0]=pad(4);
    button(5,true,0);button(6,true,1);button(4,true,2);input.pad.buttons&=~(ButtonRightBumper|ButtonNorth);tick(3);assert(generated(1)&&generated(2));
}
void legacyAndAllModes() {
    clear();auto& b=branch(0,OagSmartTrigger::Held,6,1);std::array<DiamondComboProgram,kDiamondComboSlots> old{};
    old[0].enabled=1;old[0].triggers[0].enabled=1;old[0].triggers[0].code=6;old[0].triggers[0].kind=DiamondComboTriggerKind::LogicalControl;
    assert(oagSmartLegacyMask(game,old)==1);game.programs[0].enabled=0;assert(oagSmartLegacyMask(game,old)==0);
    clear(); auto& modified=branch(0,OagSmartTrigger::Held,5,1); modified.conditionCount=2;
    modified.conditions[1].trigger=OagSmartTrigger::Double;modified.conditions[1].targets[0]=pad(4);
    old[0].triggers[0].code=5;old[1]=old[0];old[1].triggers[0].code=4;
    assert(oagSmartLegacyMask(game,old)==2); // Shared L1 modifier does not reserve L1.
    modified.conditions[1].join=OagSmartJoin::Or;assert(oagSmartLegacyMask(game,old)==3);
    modified.conditionCount=1;modified.conditions[0].trigger=OagSmartTrigger::Chord;
    modified.conditions[0].targetCount=2;modified.conditions[0].targets[1]=pad(4);
    assert(oagSmartLegacyMask(game,old)==3);
    clear();auto& km=branch(0,OagSmartTrigger::Held,5,1);km.conditions[0].targets[0]=key(224);km.conditionCount=2;
    km.conditions[1].trigger=OagSmartTrigger::Double;km.conditions[1].targets[0]=key(4);
    old={};old[0].enabled=old[0].triggers[0].enabled=1;old[0].triggers[0].kind=DiamondComboTriggerKind::KeyboardUsage;
    old[0].triggers[0].code=5;old[0].triggers[0].modifiers=1;old[1]=old[0];old[1].triggers[0].code=4;
    assert(oagSmartLegacyMask(game,old)==2); // Ctrl+A reserves A, not Ctrl+B.
    for(auto mode:{OagSmartMode::WhileHeld,OagSmartMode::StopOnRelease}){clear();auto& m=branch(0,OagSmartTrigger::Held,6,1);m.mode=mode;m.actions[0].kind=OagSmartActionKind::Hold;m.actions[0].durationMs=mode==OagSmartMode::WhileHeld?0:1000;button(6,true,0);assert(generated(1));button(6,false,1);assert(!generated(1)&&!engine.active());}
    clear();auto& again=branch(0,OagSmartTrigger::Held,6,1);again.mode=OagSmartMode::LoopUntilAgain;again.actions[0].durationMs=10;again.actions[0].releaseMs=10;
    button(6,true,0);button(6,false,1);tick(10);tick(20);tick(21);assert(generated(1));button(6,true,22);assert(!engine.active());
    clear();auto& cannot=branch(0,OagSmartTrigger::Release,6,1);cannot.cancelable=0;cannot.actions[0].durationMs=1000;
    button(6,true,0);button(6,false,1);auto starts=engine.starts();button(6,true,2);button(6,false,3);assert(engine.starts()==starts);
    (void)b;
}
void fullCapacityRecognition() {
    clear(); const auto before = engine.starts();
    for (unsigned s = 0; s < kOagSmartCombos; ++s) {
        auto& p = game.programs[s];p.enabled=1;p.branchCount=kOagSmartBranches;
        for (unsigned b = 0; b < kOagSmartBranches; ++b) {
            auto& v=p.branches[b];v.conditionCount=kOagSmartConditions;v.actions[0].targets[0]=pad(1+s%4);
            for(unsigned c=0;c<kOagSmartConditions;++c){v.conditions[c].targets[0]=pad(6);v.conditions[c].windowMs=500+s*20+b*50+c*25;}
        }
    }
    tapAt(0);tick(1024);assert(engine.starts()==before);tick(1025);
    assert(engine.starts()==before+1&&engine.lastSlot()==0&&engine.lastBranch()==0&&generated(1));
    tick(1100);assert(engine.starts()==before+1);
}
}
int main() {
    validation(); tapArbitration(); longAndRelease(); logicAndPriority(); chordSequence(); modeAndActions(); analogAndNative(); resetIsolation(); branchesElseLoopsAndWheel(); legacyAndAllModes(); fullCapacityRecognition();
    std::cout << "OAG_SMART_LOGIC_TESTS=PASS program=" << sizeof(OagSmartProgram) << " bank=" << sizeof(OagSmartGame) << " engine=" << sizeof(OagSmartComboEngine) << '\n';
}
