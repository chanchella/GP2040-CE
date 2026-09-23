#pragma once

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class PcHidOutput {
public:
    bool send(const LogicalGamepadState& state) const;
    bool sendNeutral() const;
};

} // namespace oag::firmware
