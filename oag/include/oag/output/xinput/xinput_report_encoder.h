#pragma once

#include <cstdint>

#include "oag/output/logical_gamepad_state.h"
#include "oag/output/xinput/xinput_report.h"

namespace oag {

class XinputReportEncoder {
public:
    XinputReport encode(const LogicalGamepadState& state) const;

private:
    static std::int16_t encodeAxis(std::int32_t value);
    static std::int16_t encodeYAxis(std::int32_t value);
    static std::uint8_t encodeTrigger(std::uint32_t value);
};

} // namespace oag
