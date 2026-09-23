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
    HidGamepad,
    HidKeyboard,
    HidMouse,
};

struct UsbTransportHandle {
    std::uint8_t deviceAddress = 0;
    std::uint8_t interfaceInstance = 0;
};

struct DeviceRecord {
    DeviceId id {};
    bool connected = false;
    TransportType transport = TransportType::UsbPioHost;
    ProtocolKind protocol = ProtocolKind::Unknown;
    UsbTransportHandle usb {};
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
};

class DeviceRegistry {
public:
    static constexpr std::size_t kCapacity = 8;

    std::optional<DeviceId> connectUsb(
        UsbTransportHandle handle,
        std::uint16_t vid,
        std::uint16_t pid,
        ProtocolKind protocol
    );

    bool disconnect(DeviceId id);
    bool disconnectUsb(UsbTransportHandle handle);

    const DeviceRecord* find(DeviceId id) const;
    DeviceRecord* find(DeviceId id);

    std::optional<DeviceId> findUsb(UsbTransportHandle handle) const;

private:
    std::array<DeviceRecord, kCapacity> records_ {};
    std::array<std::uint16_t, kCapacity> generations_ {};
};

} // namespace oag
