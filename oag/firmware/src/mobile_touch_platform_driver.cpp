#include "oag/firmware/mobile_touch_platform_driver.h"

namespace oag::firmware {

PlatformId MobileTouchPlatformDriver::platformId() const {
    return PlatformId::AndroidGamepad;
}

PlatformOutputCapabilities
MobileTouchPlatformDriver::capabilities() const {
    return {
        .rumble = false,
        .triggerRumble = false,
        .motion = false,
        .touch = true,
    };
}

AuthRequirement MobileTouchPlatformDriver::authRequirement() const {
    return {
        AuthRequirementKind::None,
        AuthDonorFamily::None,
    };
}

bool MobileTouchPlatformDriver::initialize() {
    return output_.sendNeutral();
}

void MobileTouchPlatformDriver::poll() {
    output_.task();
}

bool MobileTouchPlatformDriver::submit(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    return output_.send(logicalSlot, state);
}

bool MobileTouchPlatformDriver::takeRumble(
    std::uint8_t& logicalSlot,
    RumbleCommand& output
) {
    (void)logicalSlot;
    (void)output;
    return false;
}

} // namespace oag::firmware
