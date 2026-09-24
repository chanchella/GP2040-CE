#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "oag/device/device_id.h"

namespace oag {

using LogicalSlotId = std::uint8_t;

class LogicalSlotManager {
public:
    static constexpr std::size_t kGamepadSlots = 8;

    std::optional<LogicalSlotId> bindFirstFree(DeviceId device);
    bool release(DeviceId device);

    std::optional<LogicalSlotId> slotFor(DeviceId device) const;
    DeviceId deviceFor(LogicalSlotId slot) const;

private:
    std::array<DeviceId, kGamepadSlots> gamepads_ {};
};

} // namespace oag
