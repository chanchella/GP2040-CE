#include "oag/mapping/oag_weapon_tuning_engine.h"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace oag {
void OagWeaponTuningEngine::reset() { *this={}; }
std::uint16_t OagWeaponTuningEngine::shotInterval(const OagWeaponSettings& w) {
    return std::uint16_t((60000u + w.rpm/2u)/std::max<std::uint16_t>(w.rpm,1));
}
std::uint16_t OagWeaponTuningEngine::tickInterval(const OagWeaponSettings& w) {
    return w.syncRpm ? shotInterval(w) : std::max<std::uint16_t>(w.tickMs,1);
}
OagRecoilTick OagWeaponTuningEngine::tick(const OagWeaponSettings& w, bool fire, bool ads,
                                        bool reload, std::uint64_t now) {
    if (reload && !reloading_) reloadUntil_=now+std::uint64_t(w.reloadMs)*1000;
    reloading_=reload;
    if (!w.configured || !w.enabled || !fire || now<reloadUntil_) {
        firing_=false; nextTick_=0; xQ16_=yQ16_=filterX_=filterY_=0; firstPending_=true;
        return {};
    }
    if (!firing_) {
        firing_=true; fireStart_=now;
        nextTick_=now+std::uint64_t(w.startDelayMs)*1000;
        firstPending_=true;
    }
    if (now<nextTick_) return {xQ16_,yQ16_,false};
    nextTick_=now+std::uint64_t(tickInterval(w))*1000; // No catch-up burst after a stall.
    const auto elapsed=(now-fireStart_)/1000;
    const auto activeMs=elapsed>=w.startDelayMs?elapsed-w.startDelayMs:0;
    const auto shots=w.firingMode==OagFiringMode::Single?1:w.burstCount;
    bool envelope=w.firingMode==OagFiringMode::Continuous || activeMs<std::uint64_t(shots)*shotInterval(w);
    if (w.pressMs && w.releaseMs) envelope &= activeMs%(w.pressMs+w.releaseMs)<w.pressMs;
    int x=ads && w.adsOverride?w.adsHorizontal:w.horizontal;
    int y=ads && w.adsOverride?w.adsVertical:w.vertical;
    if (w.curve==OagRecoilCurve::Stages) {
        // Piecewise interpolation from base values to three named time points.
        std::uint64_t before=0; int bx=x,by=y;
        for (std::size_t i=0;i<3;++i) {
            if (activeMs<=w.stageMs[i]) {
                const auto span=w.stageMs[i]-before;
                x=bx+int(std::int64_t(w.stageX[i]-bx)*(activeMs-before)/span);
                y=by+int(std::int64_t(w.stageY[i]-by)*(activeMs-before)/span);
                break;
            }
            before=w.stageMs[i]; bx=x=w.stageX[i]; by=y=w.stageY[i];
        }
    } else if (w.rampMs && w.curve!=OagRecoilCurve::Direct) {
        std::uint64_t scale=std::min<std::uint64_t>(1000,activeMs*1000/w.rampMs);
        if (w.curve==OagRecoilCurve::EaseIn) scale=scale*scale/1000;
        x=int(std::int64_t(x)*scale/1000); y=int(std::int64_t(y)*scale/1000);
    }
    const int alpha=100-int(w.smoothing)*95/100;
    const auto filter=[&](int target,std::int32_t& state) {
        const std::int32_t q=target*65536;
        // Anti-shake suppresses tiny changes in generated compensation only.
        if (state && std::abs(std::int64_t(q)-state)<=std::int64_t(w.antiShake)*65536/10) return state;
        state+=std::int32_t(std::int64_t(q-state)*alpha/100);
        return state;
    };
    if (!envelope) { xQ16_=yQ16_=0; return {0,0,true}; }
    xQ16_=filter(x,filterX_); yQ16_=filter(y,filterY_);
    if (firstPending_) {
        if (w.firstShot) { xQ16_+=w.firstHorizontal*65536; yQ16_+=w.firstVertical*65536; }
        firstPending_=false;
    }
    return {xQ16_,yQ16_,true};
}
LogicalGamepadState OagWeaponTuningEngine::apply(LogicalGamepadState base) const {
    const auto add=[](std::int32_t old,std::int32_t raw) {
        const auto delta=std::int64_t(std::numeric_limits<std::int32_t>::max())*raw/(2000ll*65536);
        return std::int32_t(std::clamp<std::int64_t>(std::int64_t(old)+delta,
            std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()));
    };
    base.rx=add(base.rx,xQ16_); base.ry=add(base.ry,yQ16_);
    return base;
}
} // namespace oag
