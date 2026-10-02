#include "oag/mapping/oag_smart_combo_engine.h"
#include "oag/input/gamepad_state.h"
#include <algorithm>
#include <limits>

namespace oag {
namespace {
constexpr std::uint64_t ms(std::uint16_t value) { return std::uint64_t(value) * 1000; }
constexpr std::array<std::uint64_t,14> buttons {
    0, ButtonSouth, ButtonEast, ButtonWest, ButtonNorth, ButtonLeftBumper,
    ButtonRightBumper, 0, 0, ButtonLeftStick, ButtonRightStick, ButtonBack, ButtonStart, ButtonGuide
};
constexpr std::array<std::int16_t,8> dirX {1000,-1000,0,0,707,-707,707,-707};
constexpr std::array<std::int16_t,8> dirY {0,0,-1000,1000,-707,-707,707,707};
constexpr bool temporal(OagTrigger t) {
    return t != OagTrigger::Held && t != OagTrigger::Analog && t != OagTrigger::Threshold;
}
std::uint8_t wanted(const OagCondition& c) {
    return c.kind == OagTrigger::Single ? 1 : c.kind == OagTrigger::Double ? 2 :
           c.kind == OagTrigger::Triple ? 3 : c.taps;
}
}
std::size_t OagSmartComboEngine::id(OagControl c) {
    switch (c.source) {
    case OagSource::Gamepad: return c.code;
    case OagSource::Keyboard: return 17 + c.code;
    case OagSource::Mouse: return 273 + c.code;
    case OagSource::Wheel: return 289 + c.code;
    case OagSource::Stick: return 294 + c.code;
    case OagSource::Axis: return 310 + c.code;
    }
    return kControls;
}
OagControl OagSmartComboEngine::control(std::size_t n) {
    if (n <= 17) return {OagSource::Gamepad,0,std::uint16_t(n)};
    if (n < 273) return {OagSource::Keyboard,0,std::uint16_t(n-17)};
    if (n < 289) return {OagSource::Mouse,0,std::uint16_t(n-273)};
    if (n < 294) return {OagSource::Wheel,0,std::uint16_t(n-289)};
    if (n < 310) return {OagSource::Stick,0,std::uint16_t(n-294)};
    return {OagSource::Axis,0,std::uint16_t(n-310)};
}
std::int16_t OagSmartComboEngine::axis(std::uint16_t code, const LogicalGamepadState& p) {
    if (code >= 4) return std::int16_t(std::uint64_t(code == 4 ? p.leftTrigger : p.rightTrigger)*1000/65535);
    const std::array<std::int32_t,4> axes {p.lx,p.ly,p.rx,p.ry};
    return std::int16_t(std::int64_t(axes[code])*1000/std::numeric_limits<std::int32_t>::max());
}
bool OagSmartComboEngine::down(OagControl c, const OagSmartInput& in, std::int16_t threshold) {
    if (!oagValidControl(c)) return false;
    switch (c.source) {
    case OagSource::Keyboard:
        if (!in.keyboard || !in.keyboard->connected) return false;
        return c.code >= 224 ? (in.keyboard->modifiers & (1u << (c.code-224))) != 0 :
                              in.keyboard->pressed(std::uint8_t(c.code));
    case OagSource::Mouse: return in.mouse && in.mouse->connected && (in.mouse->buttons & (1u<<c.code));
    case OagSource::Wheel:
        if (!in.mouse || !in.mouse->connected) return false;
        return c.code == 1 ? in.mouse->wheel > 0 : c.code == 2 ? in.mouse->wheel < 0 :
               c.code == 3 ? in.mouse->pan > 0 : in.mouse->pan < 0;
    case OagSource::Gamepad:
        if (!in.gamepad.connected) return false;
        if (c.code == 7 || c.code == 8) return axis(c.code == 7 ? 4 : 5,in.gamepad) >= std::max<std::int16_t>(1,threshold);
        if (c.code >= 14) return (in.gamepad.dpad & (1u << (c.code-14))) != 0;
        return (in.gamepad.buttons & buttons[c.code]) != 0;
    case OagSource::Axis:
        if (!in.gamepad.connected) return false;
        return threshold < 0 ? axis(c.code,in.gamepad) <= threshold : axis(c.code,in.gamepad) >= threshold;
    case OagSource::Stick: {
        if (!in.gamepad.connected) return false;
        const auto x = axis(c.code >= 8 ? 2 : 0,in.gamepad), y = axis(c.code >= 8 ? 3 : 1,in.gamepad);
        const auto t = std::max<int>(1,std::abs(threshold));
        const int sx = x >= t ? 1 : x <= -t ? -1 : 0, sy = y >= t ? 1 : y <= -t ? -1 : 0;
        const auto d = c.code % 8;
        return sx == (dirX[d]>0?1:dirX[d]<0?-1:0) && sy == (dirY[d]>0?1:dirY[d]<0?-1:0);
    }
    }
    return false;
}
void OagSmartComboEngine::reset() {
    history_ = {}; conditions_ = {}; previous_ = {}; runs_ = {}; output_ = {};
    enabled_ = false;
    mouseSeen_=false; mouseGeneration_=0; mouseTimestamp_=0;
}
void OagSmartComboEngine::configure(const std::array<OagSmartCombo,kDiamondComboSlots>& programs) {
    reset(); programs_ = &programs;
    for (const auto& p : programs) if (p.enabled && oagValidateCombo(p)) {
        enabled_ = true;
        for (std::size_t b=0;b<p.branchCount;++b) if (p.branches[b].enabled) {
            const auto& r = p.branches[b];
            for (std::size_t i=0;i<r.conditionCount;++i) {
                const auto& c = r.conditions[i];
                auto& h = history_[id(c.control)]; h.watched = true;
                if (c.kind <= OagTrigger::Multi || c.kind == OagTrigger::Sequence || c.kind == OagTrigger::Chord)
                    h.window = std::max(h.window,c.windowMs);
                if (c.kind == OagTrigger::Long || c.kind == OagTrigger::Hold)
                    h.longLimit = h.longLimit ? std::min(h.longLimit,c.holdMs) : c.holdMs;
                if (c.kind == OagTrigger::Chord || c.kind == OagTrigger::Sequence)
                    for (std::size_t j=c.refFirst;j<c.refFirst+c.refCount;++j) {
                        auto& extra = history_[id(r.refs[j])]; extra.watched = true;
                        extra.window = std::max(extra.window,c.windowMs);
                    }
            }
        }
    }
}
void OagSmartComboEngine::cancel() {
    runs_ = {}; output_ = {};
    for (auto& h : history_) { h.taps=h.ready=0; h.cluster=false; h.consumed=h.serial; }
}
bool OagSmartComboEngine::active() const {
    for (const auto& r : runs_) if (r.running || r.latched) return true;
    return false;
}
bool OagSmartComboEngine::reserves(OagControl c) const {
    return oagValidControl(c) && history_[id(c)].watched;
}
OagSmartComboEngine::Verdict OagSmartComboEngine::evaluate(
    const OagCondition& c, const OagBranch& b, ConditionState& r, const OagSmartInput& in, std::uint64_t now) {
    auto& h = history_[id(c.control)];
    Verdict v {};
    if (c.kind <= OagTrigger::Multi) v.value = v.pulse = h.ready == wanted(c) &&
        h.lastTap-h.clusterSince <= ms(c.windowMs) && h.consumed != h.serial;
    else if (c.kind == OagTrigger::Long || c.kind == OagTrigger::Hold) {
        v.value = (h.held || h.fall) && now-h.downSince >= ms(c.holdMs) && h.consumed != h.serial;
        v.pulse = v.value && r.holdSerial != h.serial;
        if (v.pulse) r.holdSerial = h.serial;
        v.value = v.pulse;
    } else if (c.kind == OagTrigger::Release) {
        v.value = v.pulse = h.fall && h.consumed != h.serial;
    } else if (c.kind == OagTrigger::Sequence) {
        if (r.sequenceIndex && now-r.sequenceSince > ms(c.windowMs)) r.sequenceIndex=0;
        bool anyRise = false;
        for (std::size_t j=c.refFirst;j<c.refFirst+c.refCount;++j) anyRise |= history_[id(b.refs[j])].rise;
        if (anyRise) {
            const auto next = b.refs[c.refFirst+r.sequenceIndex];
            if (history_[id(next)].rise) {
                if (!r.sequenceIndex) r.sequenceSince=now;
                if (++r.sequenceIndex == c.refCount) { v.value=v.pulse=true; r.sequenceIndex=0; }
            } else {
                r.sequenceIndex=history_[id(b.refs[c.refFirst])].rise?1:0;
                r.sequenceSince=now;
            }
        }
    } else if (c.kind == OagTrigger::Chord) {
        auto earliest=now, latest=std::uint64_t(0); v.value=true;
        for (std::size_t j=c.refFirst;j<c.refFirst+c.refCount;++j) {
            const auto& t = history_[id(b.refs[j])];
            v.value &= t.held;
            earliest=std::min(earliest,t.downSince); latest=std::max(latest,t.downSince);
        }
        v.value &= latest-earliest <= ms(c.windowMs);
        v.pulse = v.value && !r.previous;
    } else {
        v.value = down(c.control,in,c.threshold);
        v.pulse = v.value && !r.previous;
    }
    r.previous = v.value;
    if (c.negate) { v.value=!v.value; v.pulse=false; }
    return v;
}
void OagSmartComboEngine::claim(const OagBranch& b, std::uint8_t conditionMask, std::array<bool,kControls>& claims, bool consume) {
    for (std::size_t i=0;i<b.conditionCount;++i) {
        if (!(conditionMask & (1u<<i))) continue;
        const auto& c=b.conditions[i]; if (c.negate) continue;
        auto mark = [&](OagControl x) {
            auto& h=history_[id(x)]; claims[id(x)]=true;
            if (consume && (i==0 || c.join==OagJoin::Or || temporal(c.kind))) {
                h.consumed=h.serial; h.ready=h.taps=0; h.cluster=false;
            }
        };
        mark(c.control);
        if (c.kind==OagTrigger::Chord || c.kind==OagTrigger::Sequence)
            for (std::size_t j=c.refFirst;j<c.refFirst+c.refCount;++j) mark(b.refs[j]);
    }
}
void OagSmartComboEngine::start(std::size_t s, std::size_t b, bool fallback, std::uint64_t now) {
    const auto& branch=(*programs_)[s].branches[b]; auto& r=runs_[s]; r={};
    r.running=true; r.branch=std::uint8_t(b); r.fallback=fallback;
    r.begin=fallback?branch.thenCount:0; r.end=fallback?branch.thenCount+branch.elseCount:branch.thenCount;
    r.step=r.begin; r.deadline=now; ++executionCount_; lastSlot_=std::uint8_t(s);
}
void OagSmartComboEngine::test(std::size_t s, std::size_t b, std::uint64_t now) {
    if (!programs_ || s>=kDiamondComboSlots || b>=(*programs_)[s].branchCount) return;
    if (!oagValidateCombo((*programs_)[s]) || !(*programs_)[s].branches[b].thenCount) return;
    enabled_=true;
    start(s,b,false,now); runs_[s].testRun=true; runs_[s].testEnd=now+10000000; // 10 s bounded test.
}
void OagSmartComboEngine::tick(const OagSmartInput& in, std::uint64_t now, bool context, std::uint16_t reloadMs) {
    if (!context || !enabled_ || !programs_) { cancel(); return; }
    const bool connected=in.gamepad.connected || (in.keyboard && in.keyboard->connected) || (in.mouse && in.mouse->connected);
    if (!connected) { cancel(); return; }
    const bool newMouse=in.mouse && (!mouseSeen_ || in.mouse->generation!=mouseGeneration_ || in.mouse->timestampUs!=mouseTimestamp_);
    if (in.mouse) { mouseSeen_=true; mouseGeneration_=in.mouse->generation; mouseTimestamp_=in.mouse->timestampUs; }
    for (std::size_t i=0;i<kControls;++i) if (history_[i].watched) {
        auto& h=history_[i]; h.ready=0;
        const auto c=control(i); const bool held=down(c,in);
        h.rise=held && !h.held; h.fall=!held && h.held;
        // Wheel/pan reports are events, including consecutive same-direction reports.
        if (c.source==OagSource::Wheel) h.rise=held && newMouse;
        if (h.rise) {
            ++h.serial; h.downSince=now;
            if (!h.cluster) { h.cluster=true; h.clusterSince=now; h.taps=0; }
            h.lastTap=now;
            if (h.taps<255) ++h.taps;
        }
        h.held=held;
        if (h.cluster && now-h.clusterSince >= ms(h.window)) {
            if (h.held && h.longLimit && now-h.downSince < ms(h.longLimit)) continue;
            h.ready=h.longLimit && (h.held || h.fall) && now-h.downSince>=ms(h.longLimit) ? 0 : h.taps;
            h.cluster=false; h.taps=0;
        }
    }
    auto& candidates=candidates_;
    std::size_t count=0;
    for (std::size_t s=0;s<kDiamondComboSlots;++s) {
        const auto& p=(*programs_)[s]; if (!p.enabled) { if (!runs_[s].testRun) runs_[s]={}; continue; }
        for (std::size_t b=0;b<p.branchCount;++b) {
            const auto& r=p.branches[b]; if (!r.enabled) continue;
            std::array<Verdict,kOagSmartConditions> v {};
            for (std::size_t i=0;i<r.conditionCount;++i)
                v[i]=evaluate(r.conditions[i],r,conditions_[s][b][i],in,now);
            bool term=v[0].value, result=false, termPulse=v[0].pulse, pulse=false;
            std::uint8_t termMask=1, conditionMask=0;
            const auto finishTerm=[&] {
                if (term) { result=true; pulse |= termPulse; conditionMask |= termMask; }
            };
            for (std::size_t i=1;i<r.conditionCount;++i) {
                if (r.conditions[i].join==OagJoin::And) {
                    term &= v[i].value; termPulse |= v[i].pulse; termMask |= std::uint8_t(1u<<i);
                } else { finishTerm(); term=v[i].value; termPulse=v[i].pulse; termMask=std::uint8_t(1u<<i); }
            }
            finishTerm();
            const bool fired=result && (pulse || !previous_[s][b]);
            const bool fallback=v[0].value && v[0].pulse && !result && r.elseCount;
            previous_[s][b]=result;
            if (!fired && !fallback) continue;
            const auto& primary=r.conditions[0]; auto& h=history_[id(primary.control)];
            if (h.consumed==h.serial && !pulse) continue;
            if (fallback) conditionMask=1;
            unsigned specificity=0;
            for (std::size_t i=0;i<r.conditionCount;++i) {
                if (!(conditionMask & (1u<<i))) continue;
                const auto& c=r.conditions[i];
                specificity += 1000 + (c.kind==OagTrigger::Sequence?400+c.refCount :
                    c.kind==OagTrigger::Chord?300+c.refCount :
                    c.kind<=OagTrigger::Multi?100+wanted(c)*10 :
                    c.kind==OagTrigger::Long || c.kind==OagTrigger::Hold?250:50);
            }
            candidates[count++]={std::uint16_t(specificity),std::uint8_t(s),std::uint8_t(b),conditionMask,fallback,false};
        }
    }
    auto& used=used_; used.fill(false);
    for (std::size_t n=0;n<count;++n) {
        std::size_t best=count;
        for (std::size_t i=0;i<count;++i)
            if (!candidates[i].used && (best==count || candidates[i].score>candidates[best].score)) best=i;
        if (best==count) break;
        auto& c=candidates[best]; c.used=true;
        const auto& p=(*programs_)[c.slot]; const auto& b=p.branches[c.branch];
        auto& claims=claims_; claims.fill(false); claim(b,c.conditions,claims,false);
        bool conflict=false;
        for (std::size_t i=0;i<kControls;++i) conflict |= used[i] && claims[i];
        if (conflict) continue;
        // One winning branch per slot, and one winner per physical gesture.
        for (const auto& x:candidates) if (x.used && &x!=&c && x.slot==c.slot && x.score==0) conflict=true;
        if (conflict) continue;
        claim(b,c.conditions,used,true);
        auto& run=runs_[c.slot];
        if ((p.mode==OagExecution::Toggle || p.mode==OagExecution::LoopUntilAgain) && (run.running || run.latched))
            run={};
        else if (run.running && p.cancelable) run={};
        else if (!run.running) start(c.slot,c.branch,c.fallback,now);
        c.score=0; // mark accepted slot.
    }
    for (std::size_t s=0;s<kDiamondComboSlots;++s) {
        auto& r=runs_[s];
        if (r.latched && (*programs_)[s].mode==OagExecution::WhileHeld &&
            !heldGate((*programs_)[s].branches[r.branch],in,r.fallback)) r={};
        if (r.running) execute(s,in,now,reloadMs);
    }
    rebuildOutput();
}
void OagSmartComboEngine::emit(OagControl c, bool held, Run& r, const OagAction* a) {
    const auto n=id(c); if (n>=kControls) return;
    const auto bit=std::uint64_t(1)<<(n%64);
    if (held) r.held[n/64]|=bit; else r.held[n/64]&=~bit;
    if (c.source==OagSource::Stick || (c.source==OagSource::Axis && a)) {
        const auto stick=c.source==OagSource::Stick?c.code/8:c.code;
        if (held) {
            r.owns |= std::uint8_t(1u<<stick);
            const int strength=a?std::clamp<int>(a->x,0,1000):1000;
            r.axes[stick*2]=c.source==OagSource::Stick?std::int16_t(dirX[c.code%8]*strength/1000):a->x;
            r.axes[stick*2+1]=c.source==OagSource::Stick?std::int16_t(dirY[c.code%8]*strength/1000):a->y;
        } else r.owns &= std::uint8_t(~(1u<<stick));
    }
    if (held && c.source==OagSource::Wheel) {
        if (c.code<=2) output_.mouse.wheel+=c.code==1?1:-1;
        else output_.mouse.pan+=c.code==3?1:-1;
        r.held[n/64]&=~bit;
    }
    // Trigger strength is stored with this step; trigger PRESS retains full scale.
    if (a && a->kind==OagActionKind::TriggerValue) {
        r.triggers[c.code==7?0:1]=a->x;
        if (held) r.owns |= c.code==7?4:8;
        else r.owns &= c.code==7?std::uint8_t(~4):std::uint8_t(~8);
    }
}
void OagSmartComboEngine::emitAction(const OagAction& a, const OagBranch& b, bool held, Run& r) {
    if (a.kind==OagActionKind::MultiPress) {
        for (std::size_t i=a.first;i<a.first+a.count;++i) emit(b.refs[i],held,r);
    } else emit(a.control,held,r,&a);
}
bool OagSmartComboEngine::heldGate(const OagBranch& b, const OagSmartInput& in, bool fallback) const {
    const auto held=[&](const OagCondition& c) {
        bool value;
        if (c.kind==OagTrigger::Chord) {
            value=true;
            for (std::size_t i=c.refFirst;i<c.refFirst+c.refCount;++i) value &= down(b.refs[i],in,c.threshold);
        } else if (c.kind==OagTrigger::Sequence) {
            value=down(b.refs[c.refFirst+c.refCount-1],in,c.threshold);
        } else value=down(c.control,in,c.threshold);
        return c.negate ? !value : value;
    };
    bool term=held(b.conditions[0]), result=false;
    if (fallback) return term;
    for (std::size_t i=1;i<b.conditionCount;++i) {
        const auto& c=b.conditions[i];
        // A tap/hold event joined with AND has already happened. Level qualifiers
        // remain live, so releasing a modifier cancels both running and latched steps.
        if (c.join==OagJoin::And) term &= temporal(c.kind) && c.kind!=OagTrigger::Chord ? true : held(c);
        else { result |= term; term=held(c); }
    }
    return result || term;
}
void OagSmartComboEngine::execute(std::size_t s, const OagSmartInput& in, std::uint64_t now, std::uint16_t reloadMs) {
    auto& r=runs_[s]; const auto& p=(*programs_)[s]; const auto& b=p.branches[r.branch];
    if (r.testRun && now>=r.testEnd) { r={}; return; }
    if (!r.testRun && (p.mode==OagExecution::WhileHeld || p.mode==OagExecution::RepeatWhileHeld ||
                      p.mode==OagExecution::StopOnRelease) && !heldGate(b,in,r.fallback)) { r={}; return; }
    for (unsigned budget=0;budget<24;++budget) { // Never block USB/BT on a loop or zero-delay sequence.
        if (now<r.deadline) return;
        if (r.step>=r.end) {
            if (!r.testRun && (p.mode==OagExecution::RepeatWhileHeld || p.mode==OagExecution::LoopUntilAgain)) {
                r.held={}; r.owns=0; r.repeats={}; r.step=r.begin; r.phase=0; r.deadline=now+1000; return;
            }
            if (!r.testRun && (p.mode==OagExecution::Toggle || p.mode==OagExecution::WhileHeld)) {
                r.latched=true; r.running=false; return;
            }
            r={}; return; // Releases only generated controls.
        }
        const auto& a=b.actions[r.step];
        if (r.phase==0) { r.phase=1; r.deadline=now+ms(a.beforeMs); if (a.beforeMs) return; }
        if (r.phase==1) {
            const auto k=a.kind;
            if (k==OagActionKind::Repeat || k==OagActionKind::Loop) {
                if (k==OagActionKind::Loop || ++r.repeats[r.step]<a.count) {
                    r.step=a.first; r.phase=0; r.deadline=now+std::max<std::uint64_t>(1000,ms(a.intervalMs)); return;
                }
                r.repeats[r.step]=0; r.phase=3;
            } else if (k==OagActionKind::ReleaseAll) { r.held={}; r.owns=0; r.phase=3; }
            else if (k==OagActionKind::Release) { emitAction(a,b,false,r); r.phase=3; }
            else if (k==OagActionKind::Press || (k==OagActionKind::Hold && !a.durationMs)) {
                emitAction(a,b,true,r); r.phase=3;
            } else {
                if (k!=OagActionKind::Wait && k!=OagActionKind::ReloadWait) emitAction(a,b,true,r);
                r.phase=2; r.pulses=0; r.pulseDown=true;
                r.deadline=now+ms(k==OagActionKind::ReloadWait?reloadMs:std::max<std::uint16_t>(a.durationMs,1));
                return;
            }
        }
        if (r.phase==2) {
            if (a.kind==OagActionKind::Pulse) {
                if (r.pulseDown) {
                    emitAction(a,b,false,r); r.pulseDown=false; ++r.pulses;
                    if (a.count && r.pulses>=a.count) { r.phase=3; r.deadline=now+ms(a.intervalMs); }
                    else { r.deadline=now+std::max<std::uint64_t>(1000,ms(a.intervalMs)); return; }
                    if (a.intervalMs) return;
                } else {
                    emitAction(a,b,true,r); r.pulseDown=true;
                    r.deadline=now+std::max<std::uint64_t>(1000,ms(a.durationMs)); return;
                }
            } else {
                if (a.kind!=OagActionKind::Wait && a.kind!=OagActionKind::ReloadWait) emitAction(a,b,false,r);
                r.phase=3;
            }
        }
        if (r.phase==3) { r.phase=4; r.deadline=now+ms(a.afterMs); if (a.afterMs) return; }
        if (r.phase==4) { ++r.step; r.phase=0; }
    }
    r.deadline=now+1000;
}
void OagSmartComboEngine::rebuildOutput() {
    const auto wheel=output_.mouse.wheel, pan=output_.mouse.pan;
    output_={}; output_.mouse.wheel=wheel; output_.mouse.pan=pan;
    for (const auto& r:runs_) if (r.running || r.latched) {
        for (std::size_t n=1;n<kControls;++n) if (r.held[n/64] & (std::uint64_t(1)<<(n%64))) {
            const auto c=control(n);
            if (c.source==OagSource::Keyboard) {
                if (c.code>=224) output_.keyboard.modifiers |= std::uint8_t(1u<<(c.code-224));
                else output_.keyboard.setPressed(std::uint8_t(c.code),true);
            } else if (c.source==OagSource::Mouse) output_.mouse.buttons|=std::uint16_t(1u<<c.code);
            else if (c.source==OagSource::Gamepad) {
                if (c.code==7 || c.code==8) {
                    auto& t=c.code==7?output_.gamepad.leftTrigger:output_.gamepad.rightTrigger;
                    if (!(r.owns&(c.code==7?4:8))) t=std::max<std::uint32_t>(t,65535);
                } else if (c.code>=14) output_.gamepad.dpad|=std::uint8_t(1u<<(c.code-14));
                else output_.gamepad.buttons|=buttons[c.code];
            }
        }
        const auto encode=[](std::int16_t x) { return std::int32_t(std::int64_t(x)*std::numeric_limits<std::int32_t>::max()/1000); };
        if (r.owns&1) { output_.gamepad.lx=encode(r.axes[0]); output_.gamepad.ly=encode(r.axes[1]); output_.stickOwnership|=1; }
        if (r.owns&2) { output_.gamepad.rx=encode(r.axes[2]); output_.gamepad.ry=encode(r.axes[3]); output_.stickOwnership|=2; }
        // Value steps override their own generated full-scale trigger only.
        if (r.owns&4) output_.gamepad.leftTrigger=std::max(output_.gamepad.leftTrigger,std::uint32_t(std::max<int>(0,r.triggers[0]))*65535/1000);
        if (r.owns&8) output_.gamepad.rightTrigger=std::max(output_.gamepad.rightTrigger,std::uint32_t(std::max<int>(0,r.triggers[1]))*65535/1000);
    }
}
LogicalGamepadState OagSmartComboEngine::merge(LogicalGamepadState base) const {
    base.buttons|=output_.gamepad.buttons; base.dpad|=output_.gamepad.dpad;
    base.leftTrigger=std::max(base.leftTrigger,output_.gamepad.leftTrigger);
    base.rightTrigger=std::max(base.rightTrigger,output_.gamepad.rightTrigger);
    if (output_.stickOwnership&1) { base.lx=output_.gamepad.lx; base.ly=output_.gamepad.ly; }
    if (output_.stickOwnership&2) { base.rx=output_.gamepad.rx; base.ry=output_.gamepad.ry; }
    base.connected |= active();
    return base;
}
} // namespace oag
