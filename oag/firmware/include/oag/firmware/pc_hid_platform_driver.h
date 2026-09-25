#pragma once

#include <cstddef>
#include <cstdint>

#include "oag/firmware/pc_hid_output.h"
#include "oag/output/platform/platform_output_driver.h"

namespace oag::firmware {

class PcHidPlatformDriver final : public IPlatformOutputDriver {
public:
    static constexpr std::size_t kOutputSlots =
        PcHidOutput::kOutputSlots;

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

    // Generic USB HID has no host-side XInput player-number assignment
    // command. Keep the TRUE GOLDEN routing call site intact and inert.
    bool takePlayerAssignment(
        std::uint8_t& receiverSlot,
        std::uint8_t& playerIndex
    );

private:
    PcHidOutput output_;
};

} // namespace oag::firmware
