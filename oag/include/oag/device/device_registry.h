#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "oag/device/device_id.h"

namespace oag {

enum class TransportType : std::uint8_t {
    UsbPioHost = 0,
    BluetoothClassic,
    BluetoothLe,
};

enum class ProtocolKind : std::uint8_t {
    Unknown = 0,
    XusbXbox360,
    XgipXboxOne,
    HidGamepad,
    HidKeyboard,
    HidMouse,
};

struct UsbTransportHandle {
    std::uint8_t deviceAddress = 0;
    std::uint8_t interfaceInstance = 0;
};

struct BluetoothTransportHandle {
    TransportType transport = TransportType::BluetoothLe;
    std::uint16_t connectionHandle = 0;
    std::uint8_t serviceInstance = 0;
};

struct DeviceRecord {
    DeviceId id {};
    bool connected = false;
    TransportType transport = TransportType::UsbPioHost;
    ProtocolKind protocol = ProtocolKind::Unknown;

    UsbTransportHandle usb {};
    BluetoothTransportHandle bluetooth {};

    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
};

class DeviceRegistry {
public:
    // Shared registry for USB + Bluetooth transports. U8A intentionally
    // preserves the hardware-verified U6E capacity invariant; capacity
    // expansion is a separate hardware/resource gate.
    static constexpr std::size_t kCapacity = 12;

    std::optional<DeviceId> connectUsb(
        UsbTransportHandle handle,
        std::uint16_t vid,
        std::uint16_t pid,
        ProtocolKind protocol
    );

    std::optional<DeviceId> connectBluetooth(
        BluetoothTransportHandle handle,
        std::uint16_t vid,
        std::uint16_t pid,
        ProtocolKind protocol
    );

    bool disconnect(DeviceId id);
    bool disconnectUsb(UsbTransportHandle handle);
    bool disconnectBluetooth(BluetoothTransportHandle handle);

    const DeviceRecord* find(DeviceId id) const;
    DeviceRecord* find(DeviceId id);

    std::optional<DeviceId> findUsb(
        UsbTransportHandle handle
    ) const;

    std::optional<DeviceId> findBluetooth(
        BluetoothTransportHandle handle
    ) const;

private:
    std::array<DeviceRecord, kCapacity> records_ {};
    std::array<std::uint16_t, kCapacity> generations_ {};

    std::optional<DeviceId> allocate(
        TransportType transport,
        ProtocolKind protocol,
        std::uint16_t vid,
        std::uint16_t pid
    );
};

} // namespace oag
