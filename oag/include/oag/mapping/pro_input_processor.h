#pragma once
#include <array>
#include "oag/config/pro_input_config.h"
#include "oag/input/mouse_state.h"
#include "oag/output/logical_gamepad_state.h"
namespace oag {
struct ProInputStats {
    DeviceId source {};
    std::uint8_t capabilities = 0;
    std::uint64_t reports = 0, firstReportUs = 0, lastReportUs = 0, windowStartedUs = 0;
    std::uint32_t windowReports = 0, reportHz = 0;
    std::uint32_t lastProcessUs = 0, maxProcessUs = 0, lastShapeUs = 0, maxShapeUs = 0;
    std::int32_t rawX = 0, rawY = 0, outputX = 0, outputY = 0;
    std::int64_t totalRawX = 0, totalRawY = 0, remainderX = 0, remainderY = 0;
};
class ProInputProcessor {
public:
    MouseMotion processMouse(DeviceId, MouseMotion, const ProInputSettings&);
    LogicalGamepadState processGamepad(const LogicalGamepadState&, const ProInputSettings&) const;
    void noteReport(DeviceId, std::uint64_t);
    void noteProcessTime(DeviceId, std::uint32_t);
    void noteShapeTime(DeviceId, std::uint32_t);
    void setCapabilities(DeviceId, std::uint8_t);
    void previewAxes(DeviceId, std::int32_t, std::int32_t, std::int32_t, std::int32_t);
    void resetFractions();
    const ProInputStats& stats(std::size_t index) const { return stats_[index]; }
private:
    ProInputStats* ensureDevice(DeviceId);
    std::array<ProInputStats, DeviceRegistry::kCapacity> stats_ {};
};
class ProProcessingClock {
public:
    bool due(std::uint64_t nowUs, std::uint16_t hz);
    void reset() { nextUs_ = 0; }
private:
    std::uint64_t nextUs_ = 0;
};
} // namespace oag
