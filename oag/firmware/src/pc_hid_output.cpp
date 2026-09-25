#include "oag/firmware/pc_hid_output.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "tusb.h"

#include "oag/input/gamepad_state.h"

namespace oag::firmware {
namespace {

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

constexpr std::uint16_t hidButton(std::uint8_t usageNumber) {
    return static_cast<std::uint16_t>(
        1u << static_cast<unsigned>(usageNumber - 1u)
    );
}

// Stable canonical face/shoulder/meta order.
//
// The first ten usages intentionally follow Microsoft's documented
// XUSB-to-HID gamepad mapping. The physical labels from other controller
// families are normalized into the same semantics before this layer:
//
//   1  South  = Xbox A      = PlayStation Cross
//   2  East   = Xbox B      = PlayStation Circle
//   3  West   = Xbox X      = PlayStation Square
//   4  North  = Xbox Y      = PlayStation Triangle
//   5  LB     = L1
//   6  RB     = R1
//   7  Back   = View/Select
//   8  Start  = Menu/Options
//   9  L3
//   10 R3
//   11 Guide  = Xbox/PS/Home
//   12 Share  = Share/Create/Capture
//   13..16 reserved for later platform-specific extensions.
constexpr std::uint16_t kButtonSouth = hidButton(1);
constexpr std::uint16_t kButtonEast = hidButton(2);
constexpr std::uint16_t kButtonWest = hidButton(3);
constexpr std::uint16_t kButtonNorth = hidButton(4);
constexpr std::uint16_t kButtonLeftBumper = hidButton(5);
constexpr std::uint16_t kButtonRightBumper = hidButton(6);
constexpr std::uint16_t kButtonBack = hidButton(7);
constexpr std::uint16_t kButtonStart = hidButton(8);
constexpr std::uint16_t kButtonLeftStick = hidButton(9);
constexpr std::uint16_t kButtonRightStick = hidButton(10);
constexpr std::uint16_t kButtonGuide = hidButton(11);
constexpr std::uint16_t kButtonShare = hidButton(12);

std::uint16_t buttonsToHid(std::uint64_t buttons) {
    std::uint16_t out = 0;

    if (buttons & ButtonSouth) out |= kButtonSouth;
    if (buttons & ButtonEast) out |= kButtonEast;
    if (buttons & ButtonWest) out |= kButtonWest;
    if (buttons & ButtonNorth) out |= kButtonNorth;
    if (buttons & ButtonLeftBumper) out |= kButtonLeftBumper;
    if (buttons & ButtonRightBumper) out |= kButtonRightBumper;
    if (buttons & ButtonBack) out |= kButtonBack;
    if (buttons & ButtonStart) out |= kButtonStart;
    if (buttons & ButtonLeftStick) out |= kButtonLeftStick;
    if (buttons & ButtonRightStick) out |= kButtonRightStick;
    if (buttons & ButtonGuide) out |= kButtonGuide;
    if (buttons & ButtonShare) out |= kButtonShare;

    return out;
}

} // namespace

void PcHidOutput::task() {
    for (std::size_t slot = 0; slot < kOutputSlots; ++slot) {
        flush(slot);
    }
}

bool PcHidOutput::send(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    if (logicalSlot >= kOutputSlots) {
        return false;
    }

    Report report {};
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

    reports_[logicalSlot] = report;
    pending_[logicalSlot] = true;

    // Endpoint busy is not a data-loss condition. The latest report remains
    // buffered and poll() retries it as soon as this HID instance is ready.
    flush(logicalSlot);
    return true;
}

bool PcHidOutput::sendNeutral(std::uint8_t logicalSlot) {
    return send(logicalSlot, LogicalGamepadState {});
}

bool PcHidOutput::flush(std::size_t slot) {
    if (slot >= kOutputSlots || !pending_[slot]) {
        return true;
    }

    if (!tud_hid_n_ready(static_cast<std::uint8_t>(slot))) {
        return false;
    }

    if (!tud_hid_n_report(
            static_cast<std::uint8_t>(slot),
            0,
            &reports_[slot],
            sizeof(Report)
        )) {
        return false;
    }

    pending_[slot] = false;
    return true;
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
