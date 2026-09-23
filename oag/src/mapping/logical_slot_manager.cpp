#include "oag/mapping/logical_slot_manager.h"

namespace oag {

std::optional<LogicalSlotId> LogicalSlotManager::bindFirstFree(DeviceId device) {
    if (!device.valid()) {
        return std::nullopt;
    }

    if (const auto existing = slotFor(device)) {
        return existing;
    }

    for (std::size_t i = 0; i < gamepads_.size(); ++i) {
        if (!gamepads_[i].valid()) {
            gamepads_[i] = device;
            return static_cast<LogicalSlotId>(i);
        }
    }

    return std::nullopt;
}

bool LogicalSlotManager::release(DeviceId device) {
    const auto slot = slotFor(device);
    if (!slot) {
        return false;
    }

    gamepads_[*slot] = {};
    return true;
}

std::optional<LogicalSlotId> LogicalSlotManager::slotFor(DeviceId device) const {
    if (!device.valid()) {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < gamepads_.size(); ++i) {
        if (gamepads_[i] == device) {
            return static_cast<LogicalSlotId>(i);
        }
    }

    return std::nullopt;
}

DeviceId LogicalSlotManager::deviceFor(LogicalSlotId slot) const {
    return slot < gamepads_.size() ? gamepads_[slot] : DeviceId {};
}

} // namespace oag
