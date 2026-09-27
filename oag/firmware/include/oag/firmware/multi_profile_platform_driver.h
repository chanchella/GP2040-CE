#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/firmware/pc_hid_platform_driver.h"
#include "oag/firmware/pc_xinput_platform_driver.h"
#include "oag/output/platform/platform_output_driver.h"

namespace oag::firmware {

class MultiProfilePlatformDriver final : public IPlatformOutputDriver {
public:
    static constexpr std::size_t kOutputSlots = 4;

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

    bool takePlayerAssignment(
        std::uint8_t& receiverSlot,
        std::uint8_t& playerIndex
    );

private:
    bool phoneProfile_ = false;
    PcXinputPlatformDriver pc_ {};
    PcHidPlatformDriver phone_ {};
};

} // namespace oag::firmware
