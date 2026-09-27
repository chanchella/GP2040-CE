#include "oag/firmware/multi_profile_platform_driver.h"

#include "oag/firmware/output_profile_selector.h"

namespace oag::firmware {

PlatformId MultiProfilePlatformDriver::platformId() const {
    return phoneProfile_
        ? PlatformId::AndroidGamepad
        : PlatformId::PcXinput360;
}

PlatformOutputCapabilities
MultiProfilePlatformDriver::capabilities() const {
    return phoneProfile_
        ? phone_.capabilities()
        : pc_.capabilities();
}

AuthRequirement MultiProfilePlatformDriver::authRequirement() const {
    return phoneProfile_
        ? phone_.authRequirement()
        : pc_.authRequirement();
}

bool MultiProfilePlatformDriver::initialize() {
    phoneProfile_ =
        activeOutputProfile() == OutputProfileId::Phone;

    return phoneProfile_
        ? phone_.initialize()
        : pc_.initialize();
}

void MultiProfilePlatformDriver::poll() {
    if (phoneProfile_) {
        phone_.poll();
    } else {
        pc_.poll();
    }
}

bool MultiProfilePlatformDriver::submit(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    return phoneProfile_
        ? phone_.submit(logicalSlot, state)
        : pc_.submit(logicalSlot, state);
}

bool MultiProfilePlatformDriver::takeRumble(
    std::uint8_t& logicalSlot,
    RumbleCommand& output
) {
    return phoneProfile_
        ? phone_.takeRumble(logicalSlot, output)
        : pc_.takeRumble(logicalSlot, output);
}

bool MultiProfilePlatformDriver::takePlayerAssignment(
    std::uint8_t& receiverSlot,
    std::uint8_t& playerIndex
) {
    if (phoneProfile_) {
        return false;
    }

    return pc_.takePlayerAssignment(
        receiverSlot,
        playerIndex
    );
}

} // namespace oag::firmware
