#include "oag/firmware/output_profile_selector.h"

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

namespace oag::firmware {
namespace {

constexpr std::uint32_t kProfileMagic = 0x4F414750u; // "OAGP"
constexpr std::uint32_t kProfileMagicIndex = 6u;
constexpr std::uint32_t kProfileValueIndex = 7u;

bool validProfileValue(std::uint32_t value) {
    return value <=
        static_cast<std::uint32_t>(OutputProfileId::OagConfig);
}

} // namespace

OutputProfileId activeOutputProfile() {
    if (
        watchdog_hw->scratch[kProfileMagicIndex] != kProfileMagic ||
        !validProfileValue(
            watchdog_hw->scratch[kProfileValueIndex]
        )
    ) {
        return OutputProfileId::Pc;
    }

    return static_cast<OutputProfileId>(
        watchdog_hw->scratch[kProfileValueIndex]
    );
}

bool outputProfileRuntimeAvailable(OutputProfileId profile) {
    switch (profile) {
        case OutputProfileId::Pc:
        case OutputProfileId::Phone:
        case OutputProfileId::MobileTouch:
        case OutputProfileId::OagConfig:
            return true;

        case OutputProfileId::XboxOne:
        case OutputProfileId::XboxSeries:
        case OutputProfileId::Playstation4:
        case OutputProfileId::Playstation5:
        case OutputProfileId::Nintendo:
        default:
            return false;
    }
}

bool requestOutputProfile(OutputProfileId profile) {
    if (!outputProfileRuntimeAvailable(profile)) {
        return false;
    }

    watchdog_hw->scratch[kProfileMagicIndex] =
        kProfileMagic;
    watchdog_hw->scratch[kProfileValueIndex] =
        static_cast<std::uint32_t>(profile);

    watchdog_reboot(0, 0, 10);
    while (true) {
        tight_loop_contents();
    }
}

bool mobileUsbProfileActive() {
    return activeOutputProfile() == OutputProfileId::Phone;
}

bool mobileTouchUsbProfileActive() {
    return activeOutputProfile() == OutputProfileId::MobileTouch;
}

std::uint8_t nativeKeyboardHidInstance() {
    return mobileUsbProfileActive() ? 4u : 0u;
}

std::uint8_t nativeMouseHidInstance() {
    return mobileUsbProfileActive() ? 5u : 1u;
}

} // namespace oag::firmware
