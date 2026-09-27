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

constexpr std::uint32_t hidButton(std::uint8_t index) {
    return static_cast<std::uint32_t>(1u) << index;
}

// Android/Linux/TinyUSB Generic HID gamepad button order.
//
// TinyUSB's canonical Linux naming is:
//   0  A / South
//   1  B / East
//   2  C (intentionally unused)
//   3  X / West
//   4  Y / North
//   5  Z (intentionally unused)
//   6  TL / L1
//   7  TR / R1
//   8  TL2 / L2
//   9  TR2 / R2
//   10 Select / Back
//   11 Start
//   12 Mode / Guide / Home
//   13 Thumb-L / L3
//   14 Thumb-R / R3
//   15 Share/Capture extension
//
// D-pad is NOT duplicated into the button bitmap. It is exposed only through
// the HID Hat Switch, preventing Android from interpreting directions as
// Home/L3/R3 buttons.
constexpr std::uint32_t kButtonSouth        = hidButton(0);
constexpr std::uint32_t kButtonEast         = hidButton(1);
constexpr std::uint32_t kButtonWest         = hidButton(3);
constexpr std::uint32_t kButtonNorth        = hidButton(4);
constexpr std::uint32_t kButtonLeftBumper   = hidButton(6);
constexpr std::uint32_t kButtonRightBumper  = hidButton(7);
constexpr std::uint32_t kButtonLeftTrigger  = hidButton(8);
constexpr std::uint32_t kButtonRightTrigger = hidButton(9);
constexpr std::uint32_t kButtonBack         = hidButton(10);
constexpr std::uint32_t kButtonStart        = hidButton(11);
constexpr std::uint32_t kButtonGuide        = hidButton(12);
constexpr std::uint32_t kButtonLeftStick    = hidButton(13);
constexpr std::uint32_t kButtonRightStick   = hidButton(14);
constexpr std::uint32_t kButtonShare        = hidButton(15);

constexpr std::uint32_t kTriggerButtonThreshold = 0x10000000u;

std::uint32_t buttonsToAndroidHid(
    const LogicalGamepadState& state
) {
    std::uint32_t out = 0;

    if (state.buttons & ButtonSouth)       out |= kButtonSouth;
    if (state.buttons & ButtonEast)        out |= kButtonEast;
    if (state.buttons & ButtonWest)        out |= kButtonWest;
    if (state.buttons & ButtonNorth)       out |= kButtonNorth;
    if (state.buttons & ButtonLeftBumper)  out |= kButtonLeftBumper;
    if (state.buttons & ButtonRightBumper) out |= kButtonRightBumper;

    // Keep trigger buttons in their Android/Linux TL2/TR2 positions while
    // preserving their analog Brake/Accelerator axes in the report.
    if (state.leftTrigger > kTriggerButtonThreshold)
        out |= kButtonLeftTrigger;
    if (state.rightTrigger > kTriggerButtonThreshold)
        out |= kButtonRightTrigger;

    if (state.buttons & ButtonBack)        out |= kButtonBack;
    if (state.buttons & ButtonStart)       out |= kButtonStart;
    if (state.buttons & ButtonGuide)       out |= kButtonGuide;
    if (state.buttons & ButtonLeftStick)   out |= kButtonLeftStick;
    if (state.buttons & ButtonRightStick)  out |= kButtonRightStick;
    if (state.buttons & ButtonShare)       out |= kButtonShare;

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
    report.lx = axisToI8(state.lx);
    report.ly = axisToI8(state.ly);
    report.rx = axisToI8(state.rx);
    report.ry = axisToI8(state.ry);
    report.leftTrigger =
        static_cast<std::uint8_t>(state.leftTrigger >> 24);
    report.rightTrigger =
        static_cast<std::uint8_t>(state.rightTrigger >> 24);
    report.hat = hatFromDpad(state.dpad);

    const std::uint32_t buttons =
        buttonsToAndroidHid(state);

    report.buttons0To7 =
        static_cast<std::uint8_t>(buttons & 0xFFu);
    report.buttons8To15 =
        static_cast<std::uint8_t>((buttons >> 8) & 0xFFu);

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
