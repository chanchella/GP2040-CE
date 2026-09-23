#include "oag/device/device_registry.h"

namespace oag {
namespace {

bool sameHandle(UsbTransportHandle lhs, UsbTransportHandle rhs) {
    return lhs.deviceAddress == rhs.deviceAddress &&
           lhs.interfaceInstance == rhs.interfaceInstance;
}

} // namespace

std::optional<DeviceId> DeviceRegistry::connectUsb(
    UsbTransportHandle handle,
    std::uint16_t vid,
    std::uint16_t pid,
    ProtocolKind protocol
) {
    if (const auto existing = findUsb(handle)) {
        return existing;
    }

    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].connected) {
            continue;
        }

        std::uint16_t next = static_cast<std::uint16_t>(generations_[i] + 1u);
        if (next == 0) {
            next = 1;
        }
        generations_[i] = next;

        DeviceRecord& record = records_[i];
        record = {};
        record.id = DeviceId {
            static_cast<std::uint16_t>(i),
            generations_[i],
        };
        record.connected = true;
        record.transport = TransportType::UsbPioHost;
        record.protocol = protocol;
        record.usb = handle;
        record.vid = vid;
        record.pid = pid;

        return record.id;
    }

    return std::nullopt;
}

bool DeviceRegistry::disconnect(DeviceId id) {
    DeviceRecord* record = find(id);
    if (record == nullptr) {
        return false;
    }

    record->connected = false;
    record->protocol = ProtocolKind::Unknown;
    record->usb = {};
    record->vid = 0;
    record->pid = 0;
    return true;
}

bool DeviceRegistry::disconnectUsb(UsbTransportHandle handle) {
    const auto id = findUsb(handle);
    return id.has_value() ? disconnect(*id) : false;
}

const DeviceRecord* DeviceRegistry::find(DeviceId id) const {
    if (!id.valid() || id.index >= records_.size()) {
        return nullptr;
    }

    const DeviceRecord& record = records_[id.index];
    if (!record.connected || record.id != id) {
        return nullptr;
    }

    return &record;
}

DeviceRecord* DeviceRegistry::find(DeviceId id) {
    return const_cast<DeviceRecord*>(
        static_cast<const DeviceRegistry*>(this)->find(id)
    );
}

std::optional<DeviceId> DeviceRegistry::findUsb(UsbTransportHandle handle) const {
    for (const DeviceRecord& record : records_) {
        if (record.connected &&
            record.transport == TransportType::UsbPioHost &&
            sameHandle(record.usb, handle)) {
            return record.id;
        }
    }

    return std::nullopt;
}

} // namespace oag
