#pragma once

#include "oag/feedback/rumble_command.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag {

enum class BluetoothOutputProfile : std::uint8_t {
    GenericHidGamepad = 0,
    XboxBleCompatible,
    DualShock4Compatible,
    DualSenseCompatible,
    SwitchProCompatible,
};

class IBluetoothInputTransport {
public:
    virtual ~IBluetoothInputTransport() = default;

    virtual bool initialize() = 0;
    virtual void poll() = 0;
    virtual bool beginDiscovery() = 0;
    virtual bool beginPairing() = 0;
};

class IBluetoothOutputPeripheral {
public:
    virtual ~IBluetoothOutputPeripheral() = default;

    virtual bool initialize(BluetoothOutputProfile profile) = 0;
    virtual void poll() = 0;
    virtual bool beginAdvertising() = 0;
    virtual bool submit(const LogicalGamepadState& state) = 0;
    virtual bool takeRumble(RumbleCommand& output) = 0;
};

} // namespace oag
