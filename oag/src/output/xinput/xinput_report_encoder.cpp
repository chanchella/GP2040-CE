#include "oag/output/xinput/xinput_report_encoder.h"

#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"

namespace oag {
namespace {

constexpr std::uint8_t kUp = 1u << 0;
constexpr std::uint8_t kDown = 1u << 1;
constexpr std::uint8_t kLeft = 1u << 2;
constexpr std::uint8_t kRight = 1u << 3;
constexpr std::uint8_t kStart = 1u << 4;
constexpr std::uint8_t kBack = 1u << 5;
constexpr std::uint8_t kLeftStick = 1u << 6;
constexpr std::uint8_t kRightStick = 1u << 7;

constexpr std::uint8_t kLeftBumper = 1u << 0;
constexpr std::uint8_t kRightBumper = 1u << 1;
constexpr std::uint8_t kGuide = 1u << 2;
constexpr std::uint8_t kSouth = 1u << 4;
constexpr std::uint8_t kEast = 1u << 5;
constexpr std::uint8_t kWest = 1u << 6;
constexpr std::uint8_t kNorth = 1u << 7;

} // namespace

XinputReport XinputReportEncoder::encode(
    const LogicalGamepadState& state
) const {
    XinputReport out {};

    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Up)) {
        out.buttons1 |= kUp;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Down)) {
        out.buttons1 |= kDown;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Left)) {
        out.buttons1 |= kLeft;
    }
    if (state.dpad & static_cast<std::uint8_t>(DpadBits::Right)) {
        out.buttons1 |= kRight;
    }

    if (state.buttons & ButtonStart) out.buttons1 |= kStart;
    if (state.buttons & ButtonBack) out.buttons1 |= kBack;
    if (state.buttons & ButtonLeftStick) out.buttons1 |= kLeftStick;
    if (state.buttons & ButtonRightStick) out.buttons1 |= kRightStick;

    if (state.buttons & ButtonLeftBumper) out.buttons2 |= kLeftBumper;
    if (state.buttons & ButtonRightBumper) out.buttons2 |= kRightBumper;
    if (state.buttons & ButtonGuide) out.buttons2 |= kGuide;
    if (state.buttons & ButtonSouth) out.buttons2 |= kSouth;
    if (state.buttons & ButtonEast) out.buttons2 |= kEast;
    if (state.buttons & ButtonWest) out.buttons2 |= kWest;
    if (state.buttons & ButtonNorth) out.buttons2 |= kNorth;

    out.leftTrigger = encodeTrigger(state.leftTrigger);
    out.rightTrigger = encodeTrigger(state.rightTrigger);

    out.lx = encodeAxis(state.lx);
    out.ly = encodeYAxis(state.ly);
    out.rx = encodeAxis(state.rx);
    out.ry = encodeYAxis(state.ry);

    return out;
}

std::int16_t XinputReportEncoder::encodeAxis(std::int32_t value) {
    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int16_t>::min();
    }

    if (value <= 0) {
        return static_cast<std::int16_t>(value / 65536);
    }

    const std::int64_t scaled =
        static_cast<std::int64_t>(value) *
        std::numeric_limits<std::int16_t>::max() /
        std::numeric_limits<std::int32_t>::max();

    return static_cast<std::int16_t>(scaled);
}

std::int16_t XinputReportEncoder::encodeYAxis(std::int32_t value) {
    // OAG canonical Y: Up is negative, Down is positive.
    // XInput wire Y: Up is positive, Down is negative.
    if (value == std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int16_t>::min();
    }

    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int16_t>::max();
    }

    return encodeAxis(-value);
}

std::uint8_t XinputReportEncoder::encodeTrigger(std::uint32_t value) {
    return static_cast<std::uint8_t>(value >> 24);
}

} // namespace oag
