#include "oag/mapping/mouse_to_stick_mapper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace oag {

StickVector MouseToStickMapper::map(
    const MouseMotion& motion,
    const MouseStickConfig& config
) const {
    const double sx =
        static_cast<double>(motion.dx) *
        std::max(0.0, config.sensitivityX);

    double sy =
        static_cast<double>(motion.dy) *
        std::max(0.0, config.sensitivityY);

    if (config.invertY) {
        sy = -sy;
    }

    if (sx == 0.0 && sy == 0.0) {
        return {};
    }

    const double exponent = sanitizeExponent(config.exponent);
    const double deadzoneX = clampDeadzone(config.deadzoneX);
    const double deadzoneY = clampDeadzone(config.deadzoneY);

    double outX = 0.0;
    double outY = 0.0;

    if (config.boundary == StickBoundary::Circle) {
        const double magnitude = std::hypot(sx, sy);
        if (magnitude == 0.0) {
            return {};
        }

        const double ux = sx / magnitude;
        const double uy = sy / magnitude;
        const double inputMagnitude = std::min(1.0, magnitude);
        const double response = std::pow(inputMagnitude, exponent);

        outX = ux * (deadzoneX + response * (1.0 - deadzoneX));
        outY = uy * (deadzoneY + response * (1.0 - deadzoneY));

        // Different X/Y deadzones can make the compensated vector slightly
        // exceed the circular stick boundary. Normalize only when necessary.
        const double outputMagnitude = std::hypot(outX, outY);
        if (outputMagnitude > 1.0) {
            outX /= outputMagnitude;
            outY /= outputMagnitude;
        }
    } else {
        const double ax = std::min(1.0, std::abs(sx));
        const double ay = std::min(1.0, std::abs(sy));

        if (ax > 0.0) {
            const double response = std::pow(ax, exponent);
            outX = std::copysign(
                deadzoneX + response * (1.0 - deadzoneX),
                sx
            );
        }

        if (ay > 0.0) {
            const double response = std::pow(ay, exponent);
            outY = std::copysign(
                deadzoneY + response * (1.0 - deadzoneY),
                sy
            );
        }
    }

    return {
        normalizedToAxis(clampUnit(outX)),
        normalizedToAxis(clampUnit(outY)),
    };
}

double MouseToStickMapper::clampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

double MouseToStickMapper::clampDeadzone(double value) {
    return std::clamp(value, 0.0, 0.95);
}

double MouseToStickMapper::sanitizeExponent(double value) {
    if (!std::isfinite(value)) {
        return 1.0;
    }

    return std::clamp(value, 0.10, 8.0);
}

std::int32_t MouseToStickMapper::normalizedToAxis(double value) {
    if (value <= -1.0) {
        return std::numeric_limits<std::int32_t>::min();
    }

    if (value >= 1.0) {
        return std::numeric_limits<std::int32_t>::max();
    }

    if (value < 0.0) {
        const double scaled =
            value *
            (static_cast<double>(
                std::numeric_limits<std::int32_t>::max()
            ) + 1.0);

        return static_cast<std::int32_t>(std::llround(scaled));
    }

    const double scaled =
        value *
        static_cast<double>(std::numeric_limits<std::int32_t>::max());

    return static_cast<std::int32_t>(std::llround(scaled));
}

} // namespace oag
