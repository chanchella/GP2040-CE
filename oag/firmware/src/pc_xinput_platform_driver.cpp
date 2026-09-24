#include "oag/firmware/pc_xinput_platform_driver.h"

namespace oag::firmware {

PlatformId PcXinputPlatformDriver::platformId() const {
    return PlatformId::PcXinput360;
}

PlatformOutputCapabilities
PcXinputPlatformDriver::capabilities() const {
    return {
        .rumble = true,
        .triggerRumble = false,
        .motion = false,
        .touch = false,
    };
}

AuthRequirement PcXinputPlatformDriver::authRequirement() const {
    return {
        AuthRequirementKind::None,
        AuthDonorFamily::None,
    };
}

bool PcXinputPlatformDriver::initialize() {
    // TinyUSB device initialization and the XInput application class driver
    // remain owned by the firmware bootstrap / PcXinputDevice translation
    // unit. No second USB initialization occurs here.
    return true;
}

void PcXinputPlatformDriver::poll() {
    device_.task();
}

bool PcXinputPlatformDriver::submit(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    // Current hardware-verified PC device profile exposes one logical XInput
    // controller. Multi-device PC output remains a later independent gate.
    if (logicalSlot != 0) {
        return false;
    }

    return device_.send(state);
}

bool PcXinputPlatformDriver::takeRumble(
    RumbleCommand& output
) {
    return device_.takeRumble(output);
}

} // namespace oag::firmware
