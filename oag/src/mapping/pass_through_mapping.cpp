#include "oag/mapping/pass_through_mapping.h"

namespace oag {

LogicalGamepadState PassThroughMapping::process(
    const UniversalGamepadState& input
) const {
    LogicalGamepadState output {};
    output.connected = input.connected;
    output.buttons = input.buttons;
    output.dpad = input.dpad;
    output.lx = input.lx;
    output.ly = input.ly;
    output.rx = input.rx;
    output.ry = input.ry;
    output.leftTrigger = input.leftTrigger;
    output.rightTrigger = input.rightTrigger;
    output.generation = input.generation;
    output.timestampUs = input.timestampUs;
    return output;
}

} // namespace oag
