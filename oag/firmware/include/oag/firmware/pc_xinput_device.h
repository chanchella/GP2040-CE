#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/feedback/rumble_command.h"
#include "oag/output/logical_gamepad_state.h"
#include "oag/output/xinput/xinput_report.h"
#include "oag/output/xinput/xinput_report_encoder.h"

namespace oag::firmware {

class PcXinputDevice {
public:
    // Windows XInput exposes at most four player slots. OAG keeps a larger
    // host-side logical-slot budget, but the live PC persona intentionally
    // exposes four independent Xbox 360-compatible outputs.
    static constexpr std::size_t kOutputSlots = 4;

    void task();

    bool send(
        std::uint8_t logicalSlot,
        const LogicalGamepadState& state
    );

    bool sendNeutral(std::uint8_t logicalSlot);

    bool takeRumble(
        std::uint8_t& logicalSlot,
        RumbleCommand& output
    );

    bool takePlayerAssignment(
        std::uint8_t& receiverSlot,
        std::uint8_t& playerIndex
    );

private:
    std::array<XinputReportEncoder, kOutputSlots> encoders_ {};
    std::array<XinputReport, kOutputSlots> reports_ {};
    std::size_t rumbleScanStart_ = 0;
};

} // namespace oag::firmware
