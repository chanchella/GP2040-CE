#include "oag/firmware/multi_profile_platform_driver.h"

#include "oag/firmware/output_profile_selector.h"

namespace oag::firmware {

PlatformId MultiProfilePlatformDriver::platformId() const {
    switch (activeMode_) {
        case ActiveMode::Phone:
            return phone_.platformId();
        case ActiveMode::MobileTouch:
            return touch_.platformId();
        case ActiveMode::Pc:
        default:
            return pc_.platformId();
    }
}

PlatformOutputCapabilities
MultiProfilePlatformDriver::capabilities() const {
    switch (activeMode_) {
        case ActiveMode::Phone:
            return phone_.capabilities();
        case ActiveMode::MobileTouch:
            return touch_.capabilities();
        case ActiveMode::Pc:
        default:
            return pc_.capabilities();
    }
}

AuthRequirement MultiProfilePlatformDriver::authRequirement() const {
    switch (activeMode_) {
        case ActiveMode::Phone:
            return phone_.authRequirement();
        case ActiveMode::MobileTouch:
            return touch_.authRequirement();
        case ActiveMode::Pc:
        default:
            return pc_.authRequirement();
    }
}

bool MultiProfilePlatformDriver::initialize() {
    switch (activeOutputProfile()) {
        case OutputProfileId::Phone:
            activeMode_ = ActiveMode::Phone;
            return phone_.initialize();

        case OutputProfileId::MobileTouch:
            activeMode_ = ActiveMode::MobileTouch;
            return touch_.initialize();

        case OutputProfileId::Pc:
        default:
            activeMode_ = ActiveMode::Pc;
            return pc_.initialize();
    }
}

void MultiProfilePlatformDriver::poll() {
    switch (activeMode_) {
        case ActiveMode::Phone:
            phone_.poll();
            return;
        case ActiveMode::MobileTouch:
            touch_.poll();
            return;
        case ActiveMode::Pc:
        default:
            pc_.poll();
            return;
    }
}

bool MultiProfilePlatformDriver::submit(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    switch (activeMode_) {
        case ActiveMode::Phone:
            return phone_.submit(logicalSlot, state);
        case ActiveMode::MobileTouch:
            return touch_.submit(logicalSlot, state);
        case ActiveMode::Pc:
        default:
            return pc_.submit(logicalSlot, state);
    }
}

bool MultiProfilePlatformDriver::takeRumble(
    std::uint8_t& logicalSlot,
    RumbleCommand& output
) {
    switch (activeMode_) {
        case ActiveMode::Phone:
            return phone_.takeRumble(logicalSlot, output);
        case ActiveMode::MobileTouch:
            return touch_.takeRumble(logicalSlot, output);
        case ActiveMode::Pc:
        default:
            return pc_.takeRumble(logicalSlot, output);
    }
}

bool MultiProfilePlatformDriver::takePlayerAssignment(
    std::uint8_t& receiverSlot,
    std::uint8_t& playerIndex
) {
    if (activeMode_ != ActiveMode::Pc) {
        return false;
    }

    return pc_.takePlayerAssignment(
        receiverSlot,
        playerIndex
    );
}

} // namespace oag::firmware
