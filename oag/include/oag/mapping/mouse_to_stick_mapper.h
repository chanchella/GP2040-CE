#pragma once

#include <cstdint>

#include "oag/input/mouse_state.h"

namespace oag {

enum class StickBoundary : std::uint8_t {
    Circle = 0,
    Square = 1,
};

struct MouseStickConfig {
    // Converts one mouse count into normalized stick motion before the curve.
    double sensitivityX = 0.01;
    double sensitivityY = 0.01;

    // 1.0 = linear. >1.0 gives finer low-speed aim and stronger high-speed
    // acceleration. Values below 1.0 are allowed for a more aggressive start.
    double exponent = 1.0;

    // Normalized compensation applied immediately to non-zero movement.
    double deadzoneX = 0.0;
    double deadzoneY = 0.0;

    // Optional stateless low-velocity precision shaping. The input velocity is
    // measured after poll-rate normalization but before sensitivity. At or
    // below precisionLowSpeed, precisionLowScale is applied. The scale then
    // ramps smoothly to 1.0 at precisionFullSpeed. This preserves fast flicks
    // while preventing the smallest mouse delta from becoming full stick.
    bool precisionBallistics = false;
    double precisionLowSpeed = 1.0;
    double precisionFullSpeed = 5.0;
    double precisionLowScale = 0.10;

    StickBoundary boundary = StickBoundary::Circle;
    bool invertY = false;
};

struct StickVector {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

class MouseToStickMapper {
public:
    StickVector map(
        const MouseMotion& motion,
        const MouseStickConfig& config,
        double motionScale = 1.0,
        double responseBoost = 1.0
    ) const;

private:
    static double clampUnit(double value);
    static double clampDeadzone(double value);
    static double sanitizeExponent(double value);
    static double smoothstep01(double value);
    static std::int32_t normalizedToAxis(double value);
};

} // namespace oag
