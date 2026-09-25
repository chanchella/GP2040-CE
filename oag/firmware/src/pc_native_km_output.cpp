#include "oag/firmware/pc_native_km_output.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "tusb.h"

namespace {

constexpr std::uint8_t kUsageF4 = 0x3D;
constexpr std::uint8_t kUsageF5 = 0x3E;

std::int8_t clampMouseAxis(std::int32_t value) {
    return static_cast<std::int8_t>(
        std::clamp<std::int32_t>(value, -127, 127)
    );
}

} // namespace

namespace oag::firmware {

void PcNativeKmOutput::setEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }

    enabled_ = enabled;

    if (!enabled_) {
        keyboardReleasePending_ = true;
        mouseReleasePending_ = true;
        pendingDx_ = 0;
        pendingDy_ = 0;
        pendingWheel_ = 0;
        pendingPan_ = 0;
        f4PressedSinceUs_ = 0;
        f5PressedSinceUs_ = 0;
    }
}

void PcNativeKmOutput::updateState(
    const KeyboardState& keyboard,
    const MouseState& mouse
) {
    keyboard_ = keyboard;
    mouse_ = mouse;
}

void PcNativeKmOutput::addMouseMotion(
    std::int32_t dx,
    std::int32_t dy,
    std::int16_t wheel,
    std::int16_t pan
) {
    if (!enabled_) {
        return;
    }

    pendingDx_ += dx;
    pendingDy_ += dy;
    pendingWheel_ += wheel;
    pendingPan_ += pan;
}

std::array<std::uint8_t, 8>
PcNativeKmOutput::buildKeyboardReport(std::uint64_t nowUs) {
    std::array<std::uint8_t, 8> report {};
    report[0] = keyboard_.modifiers;

    const bool f4Down = keyboard_.pressed(kUsageF4);
    const bool f5Down = keyboard_.pressed(kUsageF5);
    const bool chordDown = f4Down && f5Down;

    if (f4Down && f4PressedSinceUs_ == 0) {
        f4PressedSinceUs_ = nowUs;
    } else if (!f4Down) {
        f4PressedSinceUs_ = 0;
    }

    if (f5Down && f5PressedSinceUs_ == 0) {
        f5PressedSinceUs_ = nowUs;
    } else if (!f5Down) {
        f5PressedSinceUs_ = 0;
    }

    std::size_t keyIndex = 2;

    for (std::uint16_t usage = 1;
         usage < KeyboardState::kUsageCount && keyIndex < report.size();
         ++usage) {
        if (!keyboard_.pressed(static_cast<std::uint8_t>(usage))) {
            continue;
        }

        if (
            usage == kUsageF4 ||
            usage == kUsageF5
        ) {
            if (chordDown) {
                continue;
            }

            const std::uint64_t since =
                usage == kUsageF4
                    ? f4PressedSinceUs_
                    : f5PressedSinceUs_;

            if (
                since == 0 ||
                nowUs - since < kModeChordGraceUs
            ) {
                continue;
            }
        }

        report[keyIndex++] =
            static_cast<std::uint8_t>(usage);
    }

    return report;
}

void PcNativeKmOutput::task(std::uint64_t nowUs) {
    if (!enabled_) {
        if (
            keyboardReleasePending_ &&
            tud_hid_n_ready(kKeyboardInstance)
        ) {
            const std::array<std::uint8_t, 8> empty {};
            if (tud_hid_n_report(
                    kKeyboardInstance,
                    0,
                    empty.data(),
                    empty.size()
                )) {
                lastKeyboardReport_ = {};
                keyboardReleasePending_ = false;
            }
        }

        if (
            mouseReleasePending_ &&
            tud_hid_n_ready(kMouseInstance)
        ) {
            hid_mouse_report_t report {};
            if (tud_hid_n_report(
                    kMouseInstance,
                    0,
                    &report,
                    sizeof(report)
                )) {
                lastMouseButtons_ = 0;
                mouseReleasePending_ = false;
            }
        }

        return;
    }

    const auto keyboardReport =
        buildKeyboardReport(nowUs);

    if (
        keyboardReport != lastKeyboardReport_ &&
        tud_hid_n_ready(kKeyboardInstance)
    ) {
        if (tud_hid_n_report(
                kKeyboardInstance,
                0,
                keyboardReport.data(),
                keyboardReport.size()
            )) {
            lastKeyboardReport_ = keyboardReport;
        }
    }

    const std::uint8_t buttons =
        static_cast<std::uint8_t>(mouse_.buttons & 0x1Fu);

    const bool mouseChanged =
        buttons != lastMouseButtons_ ||
        pendingDx_ != 0 ||
        pendingDy_ != 0 ||
        pendingWheel_ != 0 ||
        pendingPan_ != 0;

    if (!mouseChanged || !tud_hid_n_ready(kMouseInstance)) {
        return;
    }

    const std::int8_t x = clampMouseAxis(pendingDx_);
    const std::int8_t y = clampMouseAxis(pendingDy_);
    const std::int8_t wheel = clampMouseAxis(pendingWheel_);
    const std::int8_t pan = clampMouseAxis(pendingPan_);

    hid_mouse_report_t report {
        buttons,
        x,
        y,
        wheel,
        pan,
    };

    if (!tud_hid_n_report(
            kMouseInstance,
            0,
            &report,
            sizeof(report)
        )) {
        return;
    }

    lastMouseButtons_ = buttons;
    pendingDx_ -= x;
    pendingDy_ -= y;
    pendingWheel_ -= wheel;
    pendingPan_ -= pan;
}

void PcNativeKmOutput::releaseAll() {
    setEnabled(false);
}

} // namespace oag::firmware
