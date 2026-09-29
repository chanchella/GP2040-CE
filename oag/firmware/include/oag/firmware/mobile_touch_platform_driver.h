#pragma once

#include "oag/firmware/mobile_touch_output.h"
#include "oag/output/platform/platform_output_driver.h"

namespace oag::firmware {

class MobileTouchPlatformDriver final : public IPlatformOutputDriver {
public:
    PlatformId platformId() const override;
    PlatformOutputCapabilities capabilities() const override;
    AuthRequirement authRequirement() const override;

    bool initialize() override;
    void poll() override;

    bool submit(
        std::uint8_t logicalSlot,
        const LogicalGamepadState& state
    ) override;

    bool takeRumble(
        std::uint8_t& logicalSlot,
        RumbleCommand& output
    ) override;

private:
    MobileTouchOutput output_ {};
};

} // namespace oag::firmware
