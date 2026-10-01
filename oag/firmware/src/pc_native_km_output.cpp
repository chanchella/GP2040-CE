#include "oag/firmware/pc_native_km_output.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "tusb.h"

#include "oag/firmware/output_profile_selector.h"

namespace {

constexpr std::uint8_t kUsageF1 = 0x3A;
constexpr std::uint8_t kUsageF4 = 0x3D;
constexpr std::uint8_t kUsageF5 = 0x3E;
constexpr std::uint8_t kUsageF8 = 0x41;
constexpr std::uint8_t kUsageF9 = 0x42;

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
        pendingMouse_.clear();
        f1PressedSinceUs_ = 0;
        f4PressedSinceUs_ = 0;
        f5PressedSinceUs_ = 0;
        f8PressedSinceUs_ = 0;
        f9PressedSinceUs_ = 0;
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

    pendingMouse_.add(dx, dy, wheel, pan);
}

std::array<std::uint8_t, 8>
PcNativeKmOutput::buildKeyboardReport(std::uint64_t nowUs) {
    std::array<std::uint8_t, 8> report {};
    report[0] = keyboard_.modifiers;

    const bool f1Down = keyboard_.pressed(kUsageF1);
    if (f1Down && f1PressedSinceUs_ == 0) f1PressedSinceUs_ = nowUs;
    else if (!f1Down) f1PressedSinceUs_ = 0;
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

    const bool f8Down = keyboard_.pressed(kUsageF8);
    const bool f9Down = keyboard_.pressed(kUsageF9);
    const bool profileChordDown = f8Down && f9Down;

    if (f8Down && f8PressedSinceUs_ == 0) {
        f8PressedSinceUs_ = nowUs;
    } else if (!f8Down) {
        f8PressedSinceUs_ = 0;
    }

    if (f9Down && f9PressedSinceUs_ == 0) {
        f9PressedSinceUs_ = nowUs;
    } else if (!f9Down) {
        f9PressedSinceUs_ = 0;
    }

    std::size_t keyIndex = 2;

    for (std::uint16_t usage = 1;
         usage < KeyboardState::kUsageCount && keyIndex < report.size();
         ++usage) {
        if (!keyboard_.pressed(static_cast<std::uint8_t>(usage))) {
            continue;
        }

        if (usage == kUsageF1 && (f1PressedSinceUs_ == 0 || nowUs - f1PressedSinceUs_ < kProfileChordGraceUs)) continue;

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

        if (
            usage == kUsageF8 ||
            usage == kUsageF9
        ) {
            if (profileChordDown) {
                continue;
            }

            const std::uint64_t since =
                usage == kUsageF8
                    ? f8PressedSinceUs_
                    : f9PressedSinceUs_;

            if (
                since == 0 ||
                nowUs - since < kProfileChordGraceUs
            ) {
                continue;
            }
        }

        report[keyIndex++] =
            static_cast<std::uint8_t>(usage);
    }

    return report;
}

void PcNativeKmOutput::task(std::uint64_t nowUs, bool keyboardTick, bool mouseTick) {
    // Neutral must be transmitted before a state following a context switch,
    // even if output was already re-enabled while the endpoint was busy.
    if (keyboardReleasePending_ || mouseReleasePending_) {
        if (keyboardReleasePending_ && tud_hid_n_ready(nativeKeyboardHidInstance())) {
            const std::array<std::uint8_t, 8> empty {};
            if (tud_hid_n_report(nativeKeyboardHidInstance(), 0, empty.data(), empty.size())) {
                lastKeyboardReport_ = {}; keyboardReleasePending_ = false;
            }
        }
        if (mouseReleasePending_ && tud_hid_n_ready(nativeMouseHidInstance())) {
            const auto empty = oag::NativeMouseQueue {}.packet(0, tud_hid_n_get_protocol(nativeMouseHidInstance()) == HID_PROTOCOL_BOOT);
            if (tud_hid_n_report(nativeMouseHidInstance(), 0, empty.bytes.data(), empty.length)) {
                lastMouseButtons_ = 0; mouseReleasePending_ = false;
            }
        }
        return;
    }
    if (!enabled_) return;
    const auto keys = buildKeyboardReport(nowUs);
    if (keyboardTick && keys != lastKeyboardReport_ && tud_hid_n_ready(nativeKeyboardHidInstance()) &&
        tud_hid_n_report(nativeKeyboardHidInstance(), 0, keys.data(), keys.size())) lastKeyboardReport_ = keys;
    const std::uint8_t buttons = mouse_.buttons & 31u;
    if (!mouseTick || (buttons == lastMouseButtons_ && !pendingMouse_.pending()) || !tud_hid_n_ready(nativeMouseHidInstance())) return;
    const auto packet = pendingMouse_.packet(buttons, tud_hid_n_get_protocol(nativeMouseHidInstance()) == HID_PROTOCOL_BOOT);
    if (tud_hid_n_report(nativeMouseHidInstance(), 0, packet.bytes.data(), packet.length)) {
        lastMouseButtons_ = buttons; pendingMouse_.sent(packet);
    }
}
void PcNativeKmOutput::releaseAll() {
    setEnabled(false); pendingMouse_.clear();
    keyboardReleasePending_ = mouseReleasePending_ = true;
}
} // namespace oag::firmware
