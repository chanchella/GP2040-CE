#include "oag/mapping/pro_input_processor.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace oag {
bool validProInputSettings(const ProInputSettings& s) {
    return s.sourceDpi <= 64000 && s.targetDpi >= 100 && s.targetDpi <= 64000 &&
        s.multiplierPermille >= 10 && s.multiplierPermille <= 32000 &&
        s.gainXPermille >= 10 && s.gainXPermille <= 8000 &&
        s.gainYPermille >= 10 && s.gainYPermille <= 8000 &&
        (s.processingHz == 125 || s.processingHz == 250 || s.processingHz == 500 || s.processingHz == 1000) &&
        s.curvePermille >= 100 && s.curvePermille <= 4000 &&
        s.innerDeadzonePermille <= 400 && s.outerDeadzonePermille <= 400 &&
        s.mouseFullScaleCounts >= 1 && s.mouseFullScaleCounts <= 32767 &&
        static_cast<unsigned>(s.dpiSource) <= 2 &&
        ((s.sourceDpi == 0) == (s.dpiSource == ProDpiSource::Unknown));
}
ProInputKind proInputKind(ProtocolKind p) {
    return p == ProtocolKind::HidMouse ? ProInputKind::Mouse :
        p == ProtocolKind::HidKeyboard ? ProInputKind::Keyboard : ProInputKind::Gamepad;
}
int proProfileSlot(const ProInputConfig& c, const DeviceRecord& d, ProInputKind kind) {
    int free = -1;
    for (std::size_t i = 0; i < c.devices.size(); ++i) {
        const auto& p = c.devices[i];
        if (!p.enabled) { if (free < 0) free = static_cast<int>(i); }
        else if (p.vid == d.vid && p.pid == d.pid && p.transport == d.transport && p.kind == kind)
            return static_cast<int>(i);
    }
    return free;
}
const ProInputSettings& proSettingsFor(const ProInputConfig& c, const DeviceRecord& d, ProInputKind k) {
    const int slot = proProfileSlot(c, d, k);
    if (slot >= 0 && c.devices[slot].enabled) return c.devices[slot].settings;
    return c.defaults[static_cast<unsigned>(k)];
}
std::int32_t proDpiGainQ16(const ProInputSettings& s) {
    const std::int64_t q = s.automaticMultiplier && s.sourceDpi != 0 ?
        (std::int64_t(s.targetDpi) * 65536 + s.sourceDpi / 2) / s.sourceDpi :
        (std::int64_t(s.multiplierPermille) * 65536 + 500) / 1000;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(q, 655, 32 * 65536));
}
ProInputStats* ProInputProcessor::ensureDevice(DeviceId d) {
    if (!d.valid() || d.index >= stats_.size()) return nullptr;
    auto& s = stats_[d.index];
    if (s.source != d) { s = {}; s.source = d; }
    return &s;
}
MouseMotion ProInputProcessor::processMouse(DeviceId d, MouseMotion raw, const ProInputSettings& p) {
    auto* s = ensureDevice(d); if (!s) return raw;
    s->rawX = raw.dx; s->rawY = raw.dy;
    s->totalRawX += raw.dx; s->totalRawY += raw.dy;
    const auto q = proDpiGainQ16(p);
    auto axis = [&](std::int32_t v, std::uint16_t gain, std::int64_t& remainder) {
        const std::int64_t aq = gain == 1000 ? q : (std::int64_t(q) * gain + 500) / 1000;
        const std::int64_t total = std::int64_t(v) * aq + (p.fractionalRemainder ? remainder : 0);
        remainder = p.fractionalRemainder ? total % 65536 : 0;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(total / 65536, INT32_MIN, INT32_MAX));
    };
    s->outputX = axis(raw.dx, p.gainXPermille, s->remainderX);
    s->outputY = axis(raw.dy, p.gainYPermille, s->remainderY);
    return {s->outputX, s->outputY};
}
LogicalGamepadState ProInputProcessor::processGamepad(const LogicalGamepadState& raw, const ProInputSettings& s) const {
    auto result = raw;
    auto axis = [&](std::int32_t v, std::uint16_t gain) {
        if (gain == 1000 && s.curvePermille == 1000 && s.innerDeadzonePermille == 0 && s.outerDeadzonePermille == 0) return v;
        const double denominator = v < 0 ? 2147483648.0 : 2147483647.0;
        double a = std::abs(v / denominator);
        const double inner = s.innerDeadzonePermille / 1000.0;
        if (a <= inner) return std::int32_t(0);
        a = std::clamp((a - inner) / (1.0 - s.outerDeadzonePermille / 1000.0 - inner), 0.0, 1.0);
        if (s.curvePermille != 1000) a = std::pow(a, s.curvePermille / 1000.0);
        a = std::min(1.0, a * gain / 1000.0);
        if (a >= 1) return v < 0 ? INT32_MIN : INT32_MAX;
        return static_cast<std::int32_t>(std::llround(std::copysign(a * denominator, v)));
    };
    result.lx = axis(raw.lx, s.gainXPermille); result.rx = axis(raw.rx, s.gainXPermille);
    result.ly = axis(raw.ly, s.gainYPermille); result.ry = axis(raw.ry, s.gainYPermille);
    return result;
}
void ProInputProcessor::noteReport(DeviceId d, std::uint64_t now) {
    auto* s = ensureDevice(d); if (!s) return;
    if (!s->reports) { s->firstReportUs = now; s->windowStartedUs = now; }
    ++s->reports; ++s->windowReports; s->lastReportUs = now;
    const auto elapsed = now - s->windowStartedUs;
    if (elapsed >= 500000) {
        s->reportHz = static_cast<std::uint32_t>(std::uint64_t(s->windowReports) * 1000000 / elapsed);
        s->windowStartedUs = now; s->windowReports = 0;
    }
}
void ProInputProcessor::noteProcessTime(DeviceId d, std::uint32_t us) {
    auto* s = ensureDevice(d); if (s) { s->lastProcessUs = us; s->maxProcessUs = std::max(s->maxProcessUs, us); }
}
void ProInputProcessor::noteShapeTime(DeviceId d, std::uint32_t us) {
    auto* s = ensureDevice(d); if (s) { s->lastShapeUs = us; s->maxShapeUs = std::max(s->maxShapeUs, us); }
}
void ProInputProcessor::setCapabilities(DeviceId d, std::uint8_t caps) { auto* s = ensureDevice(d); if (s) s->capabilities = caps; }
void ProInputProcessor::previewAxes(DeviceId d, std::int32_t x, std::int32_t y, std::int32_t ox, std::int32_t oy) {
    auto* s = ensureDevice(d); if (s) { s->rawX = x; s->rawY = y; s->outputX = ox; s->outputY = oy; }
}
void ProInputProcessor::resetFractions() { for (auto& s : stats_) s.remainderX = s.remainderY = 0; }
bool ProProcessingClock::due(std::uint64_t now, std::uint16_t hz) {
    if (now < nextUs_) return false;
    nextUs_ = now + 1000000 / std::clamp<unsigned>(hz, 125, 1000); return true;
}
} // namespace oag
