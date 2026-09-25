#include "oag/firmware/pc_hid_platform_driver.h"

namespace oag::firmware {

PlatformId PcHidPlatformDriver::platformId() const {
    return PlatformId::PcGenericHid;
}

PlatformOutputCapabilities
PcHidPlatformDriver::capabilities() const {
    return {
        .rumble = false,
        .triggerRumble = false,
        .motion = false,
        .touch = false,
    };
}

AuthRequirement PcHidPlatformDriver::authRequirement() const {
    return {
        AuthRequirementKind::None,
        AuthDonorFamily::None,
    };
}

bool PcHidPlatformDriver::initialize() {
    // Queue neutral states before Windows finishes enumeration. They remain
    // buffered until each HID instance becomes ready.
    for (std::size_t slot = 0; slot < kOutputSlots; ++slot) {
        output_.sendNeutral(static_cast<std::uint8_t>(slot));
    }

    return true;
}

void PcHidPlatformDriver::poll() {
    output_.task();
}

bool PcHidPlatformDriver::submit(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    return output_.send(logicalSlot, state);
}

bool PcHidPlatformDriver::takeRumble(
    std::uint8_t& logicalSlot,
    RumbleCommand& output
) {
    (void)logicalSlot;
    (void)output;

    // This isolated PC-HID candidate intentionally changes only the host
    // controller persona. Standard generic HID does not provide XInput rumble.
    return false;
}

bool PcHidPlatformDriver::takePlayerAssignment(
    std::uint8_t& receiverSlot,
    std::uint8_t& playerIndex
) {
    (void)receiverSlot;
    (void)playerIndex;
    return false;
}

} // namespace oag::firmware
