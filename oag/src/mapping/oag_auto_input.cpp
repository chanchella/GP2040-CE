#include "oag/mapping/oag_auto_input.h"
#include <algorithm>
#include <limits>
namespace oag {
const ProInputSettings& OagAutoInput::settings(ProInputKind kind) {
    static const std::array<ProInputSettings,3> profiles=[] {
        std::array<ProInputSettings,3> p {};
        for (auto& s:p) { s.targetDpi=2400; s.processingHz=1000; }
        p[0].mouseFullScaleCounts=16; p[0].curvePermille=1100;
        p[2].innerDeadzonePermille=15; p[2].curvePermille=1050;
        return p;
    }();
    return profiles[static_cast<unsigned>(kind)];
}
OagAutoInput::State* OagAutoInput::state(DeviceId device) {
    if (!device.valid() || device.index>=devices_.size()) return nullptr;
    auto& s=devices_[device.index];
    if (s.device!=device) { s={}; s.device=device; }
    return &s;
}
void OagAutoInput::reset() { devices_={}; }
MouseMotion OagAutoInput::mouse(DeviceId device,MouseMotion raw,std::uint64_t now) {
    auto* s=state(device); if (!s) return raw;
    const auto safe=[](std::int64_t v) { return std::int32_t(std::clamp<std::int64_t>(v,INT32_MIN,INT32_MAX)); };
    // Gentle one-frame split at low speed; fine one-count motions and fast flicks are immediate.
    const auto speed=std::abs(std::int64_t(raw.dx))+std::abs(std::int64_t(raw.dy));
    const MouseMotion tail=speed>=2 && speed<=16?MouseMotion{raw.dx/4,raw.dy/4}:MouseMotion{};
    const MouseMotion out {safe(std::int64_t(raw.dx)-tail.dx+s->pending.dx),safe(std::int64_t(raw.dy)-tail.dy+s->pending.dy)};
    s->pending=tail; s->due=now+1000; return out;
}
MouseMotion OagAutoInput::flush(DeviceId device,std::uint64_t now) {
    auto* s=state(device); if (!s || now<s->due) return {};
    const auto out=s->pending; s->pending={}; return out;
}
LogicalGamepadState OagAutoInput::gamepad(DeviceId device,const LogicalGamepadState& raw) {
    auto* s=state(device); if (!s) return raw;
    auto result=raw; const std::array<std::int32_t,4> axes {raw.lx,raw.ly,raw.rx,raw.ry};
    std::array<std::int32_t*,4> dst {&result.lx,&result.ly,&result.rx,&result.ry};
    for (std::size_t i=0;i<axes.size();++i) {
        const auto v=axes[i],old=s->axes[i];
        const auto delta=std::int64_t(v)-old;
        // Neutral, reversals, endpoints and larger moves snap immediately; only micro changes blend.
        if (s->padSeen && v && ((v<0)==(old<0)) && std::abs(delta)<INT32_MAX/32 && std::abs(std::int64_t(v))<INT32_MAX-INT32_MAX/32)
            *dst[i]=std::int32_t((std::int64_t(v)*7+old)/8);
        s->axes[i]=*dst[i];
    }
    s->padSeen=true; return result; // Buttons, hat, triggers and connection fields remain exact.
}
} // namespace oag
