#pragma once

#include "oag/feedback/rumble_command.h"
#include "oag/output/logical_gamepad_state.h"
#include "oag/output/xinput/xinput_report.h"
#include "oag/output/xinput/xinput_report_encoder.h"

namespace oag::firmware {

class PcXinputDevice {
public:
    void task();

    bool send(const LogicalGamepadState& state);
    bool sendNeutral();

    bool takeRumble(RumbleCommand& output);

private:
    XinputReportEncoder encoder_;
    XinputReport report_ {};
};

} // namespace oag::firmware
