#include "oag/firmware/pc_hid_output.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "tusb.h"

#include "oag/input/gamepad_state.h"

namespace oag::firmware {
namespace {

constexpr std::uint8_t kGamepadReportId = 1;

struct __attribute__((packed)) PcGamepadReport {
    std::uint16_t buttons;
    std::uint8_t hat;
    std::int8_t lx;
    std::int8_t ly;
    std::int8_t rx;
    std::int8_t ry;
    std::uint8_t leftTrigger;
    std::uint8_t rightTrigger;
};

std::int8_t axisToI8(std::int32_t value) {
    const std::int64_t scaled =
        static_cast<std::int64_t>(value) * 127 /
        std::numeric_limits<std::int32_t>::max();

    return static_cast<std::int8_t>(
        std::clamp<std::int64_t>(scaled, -127, 127)
    );
}

std::uint8_t hatFromDpad(std::uint8_t dpad) {
    const bool up =
        (dpad & static_cast<std::uint8_t>(DpadBits::Up)) != 0;
    const bool down =
        (dpad & static_cast<std::uint8_t>(DpadBits::Down)) != 0;
    const bool left =
        (dpad & static_cast<std::uint8_t>(DpadBits::Left)) != 0;
    const bool right =
        (dpad & static_cast<std::uint8_t>(DpadBits::Right)) != 0;

    if (up && right && !down && !left) return 1;
    if (right && !up && !down && !left) return 2;
    if (down && right && !up && !left) return 3;
    if (down && !up && !left && !right) return 4;
    if (down && left && !up && !right) return 5;
    if (left && !up && !down && !right) return 6;
    if (up && left && !down && !right) return 7;
    if (up && !down && !left && !right) return 0;

    return 8;
}

std::uint16_t buttonsToHid(std::uint64_t buttons) {
    std::uint16_t out = 0;

    if (buttons & ButtonSouth) out |= 1u << 0;
    if (buttons & ButtonEast) out |= 1u << 1;
    if (buttons & ButtonWest) out |= 1u << 2;
    if (buttons & ButtonNorth) out |= 1u << 3;
    if (buttons & ButtonLeftBumper) out |= 1u << 4;
    if (buttons & ButtonRightBumper) out |= 1u << 5;
    if (buttons & ButtonBack) out |= 1u << 6;
    if (buttons & ButtonStart) out |= 1u << 7;
    if (buttons & ButtonLeftStick) out |= 1u << 8;
    if (buttons & ButtonRightStick) out |= 1u << 9;
    if (buttons & ButtonGuide) out |= 1u << 10;

    return out;
}

} // namespace

bool PcHidOutput::send(const LogicalGamepadState& state) const {
    if (!tud_hid_ready()) {
        return false;
    }

    PcGamepadReport report {};
    report.buttons = buttonsToHid(state.buttons);
    report.hat = hatFromDpad(state.dpad);
    report.lx = axisToI8(state.lx);
    report.ly = axisToI8(state.ly);
    report.rx = axisToI8(state.rx);
    report.ry = axisToI8(state.ry);
    report.leftTrigger =
        static_cast<std::uint8_t>(state.leftTrigger >> 24);
    report.rightTrigger =
        static_cast<std::uint8_t>(state.rightTrigger >> 24);

    return tud_hid_report(
        kGamepadReportId,
        &report,
        sizeof(report)
    );
}

bool PcHidOutput::sendNeutral() const {
    return send(LogicalGamepadState {});
}

} // namespace oag::firmware

extern "C" std::uint16_t tud_hid_get_report_cb(
    std::uint8_t instance,
    std::uint8_t report_id,
    hid_report_type_t report_type,
    std::uint8_t* buffer,
    std::uint16_t reqlen
) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

extern "C" void tud_hid_set_report_cb(
    std::uint8_t instance,
    std::uint8_t report_id,
    hid_report_type_t report_type,
    std::uint8_t const* buffer,
    std::uint16_t bufsize
) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
