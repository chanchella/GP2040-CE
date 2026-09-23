#pragma once

#include "oag/input/gamepad_state.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

class PassThroughMapping {
public:
    LogicalGamepadState process(const UniversalGamepadState& input) const;
};

} // namespace oag
