#include "oag/device/device_registry.h"

namespace oag {
namespace {

bool sameHandle(
    UsbTransportHandle lhs,
    UsbTransportHandle rhs
) {
    return lhs.deviceAddress == rhs.deviceAddress &&
           lhs.interfaceInstance == rhs.interfaceInstance;
}

bool sameHandle(
    BluetoothTransportHandle lhs,
    BluetoothTransportHandle rhs
) {
    return lhs.transport == rhs.transport &&
           lhs.connectionHandle == rhs.connectionHandle &&
           lhs.serviceInstance == rhs.serviceInstance;
}

} // namespace

std::optional<DeviceId> DeviceRegistry::allocate(
    TransportType transport,
    ProtocolKind protocol,
    std::uint16_t vid,
    std::uint16_t pid
) {
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].connected) {
            continue;
        }

        std::uint16_t next =
            static_cast<std::uint16_t>(
                generations_[i] + 1u
            );

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
        record.transport = transport;
        record.protocol = protocol;
        record.vid = vid;
        record.pid = pid;

        return record.id;
    }

    return std::nullopt;
}

std::optional<DeviceId> DeviceRegistry::connectUsb(
    UsbTransportHandle handle,
    std::uint16_t vid,
    std::uint16_t pid,
    ProtocolKind protocol
) {
    if (const auto existing = findUsb(handle)) {
        return existing;
    }

    const auto id = allocate(
        TransportType::UsbPioHost,
        protocol,
        vid,
        pid
    );

    if (!id) {
        return std::nullopt;
    }

    DeviceRecord* record = find(*id);
    if (record == nullptr) {
        return std::nullopt;
    }

    record->usb = handle;
    return id;
}

std::optional<DeviceId> DeviceRegistry::connectBluetooth(
    BluetoothTransportHandle handle,
    std::uint16_t vid,
    std::uint16_t pid,
    ProtocolKind protocol
) {
    if (
        handle.transport != TransportType::BluetoothClassic &&
        handle.transport != TransportType::BluetoothLe
    ) {
        return std::nullopt;
    }

    if (const auto existing = findBluetooth(handle)) {
        return existing;
    }

    const auto id = allocate(
        handle.transport,
        protocol,
        vid,
        pid
    );

    if (!id) {
        return std::nullopt;
    }

    DeviceRecord* record = find(*id);
    if (record == nullptr) {
        return std::nullopt;
    }

    record->bluetooth = handle;
    return id;
}

bool DeviceRegistry::disconnect(DeviceId id) {
    DeviceRecord* record = find(id);
    if (record == nullptr) {
        return false;
    }

    record->connected = false;
    record->protocol = ProtocolKind::Unknown;
    record->usb = {};
    record->bluetooth = {};
    record->vid = 0;
    record->pid = 0;
    return true;
}

bool DeviceRegistry::disconnectUsb(
    UsbTransportHandle handle
) {
    const auto id = findUsb(handle);
    return id.has_value() ? disconnect(*id) : false;
}

bool DeviceRegistry::disconnectBluetooth(
    BluetoothTransportHandle handle
) {
    const auto id = findBluetooth(handle);
    return id.has_value() ? disconnect(*id) : false;
}

const DeviceRecord* DeviceRegistry::find(
    DeviceId id
) const {
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

std::optional<DeviceId> DeviceRegistry::findUsb(
    UsbTransportHandle handle
) const {
    for (const DeviceRecord& record : records_) {
        if (
            record.connected &&
            record.transport == TransportType::UsbPioHost &&
            sameHandle(record.usb, handle)
        ) {
            return record.id;
        }
    }

    return std::nullopt;
}

std::optional<DeviceId> DeviceRegistry::findBluetooth(
    BluetoothTransportHandle handle
) const {
    for (const DeviceRecord& record : records_) {
        if (
            record.connected &&
            (
                record.transport == TransportType::BluetoothClassic ||
                record.transport == TransportType::BluetoothLe
            ) &&
            sameHandle(record.bluetooth, handle)
        ) {
            return record.id;
        }
    }

    return std::nullopt;
}

} // namespace oag
