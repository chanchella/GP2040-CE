#pragma once

#include <cstdint>

namespace oag::firmware {

enum class OutputProfileId : std::uint8_t {
    Pc = 0,
    Phone = 1,
    XboxOne = 2,
    XboxSeries = 3,
    Playstation4 = 4,
    Playstation5 = 5,
    Nintendo = 6,
    MobileTouch = 7,
};

OutputProfileId activeOutputProfile();

bool outputProfileRuntimeAvailable(OutputProfileId profile);

// Persists the requested profile across the controlled watchdog reboot only.
// A real power cycle intentionally falls back to PC.
bool requestOutputProfile(OutputProfileId profile);

bool mobileUsbProfileActive();
bool mobileTouchUsbProfileActive();

std::uint8_t nativeKeyboardHidInstance();
std::uint8_t nativeMouseHidInstance();

} // namespace oag::firmware
