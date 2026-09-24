#pragma once

#include "oag/firmware/pc_xinput_device.h"
#include "oag/output/platform/platform_output_driver.h"

namespace oag::firmware {

class PcXinputPlatformDriver final : public IPlatformOutputDriver {
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

    bool takeRumble(RumbleCommand& output) override;

private:
    PcXinputDevice device_;
};

} // namespace oag::firmware
