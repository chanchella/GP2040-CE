#include <cassert>
#include <cstring>
#include <iostream>

#include "oag/core/product_identity.h"
#include "oag/device/device_registry.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/pass_through_mapping.h"

using namespace oag;

int main() {
    assert(std::strcmp(
        product::kProductName,
        "OAG Abo Gemi Ultra Gaming"
    ) == 0);

    DeviceRegistry registry;

    const UsbTransportHandle firstHandle {1, 0};
    const auto first = registry.connectUsb(
        firstHandle,
        0x045E,
        0x028E,
        ProtocolKind::XusbXbox360
    );
    assert(first.has_value());
    assert(first->valid());

    const DeviceRecord* firstRecord = registry.find(*first);
    assert(firstRecord != nullptr);
    assert(firstRecord->vid == 0x045E);
    assert(firstRecord->pid == 0x028E);
    assert(firstRecord->protocol == ProtocolKind::XusbXbox360);

    LogicalSlotManager slots;
    const auto slot0 = slots.bindFirstFree(*first);
    assert(slot0.has_value() && *slot0 == 0);

    assert(registry.disconnect(*first));
    assert(registry.find(*first) == nullptr);
    assert(slots.release(*first));

    const auto second = registry.connectUsb(
        firstHandle,
        0x045E,
        0x028E,
        ProtocolKind::XusbXbox360
    );
    assert(second.has_value());
    assert(second->index == first->index);
    assert(second->generation != first->generation);
    assert(registry.find(*first) == nullptr);
    assert(registry.find(*second) != nullptr);

    const auto rebound = slots.bindFirstFree(*second);
    assert(rebound.has_value() && *rebound == 0);

    UniversalGamepadState input {};
    input.source = *second;
    input.connected = true;
    input.buttons = ButtonSouth | ButtonGuide;
    input.dpad = static_cast<std::uint8_t>(DpadBits::Up);
    input.lx = -123456;
    input.ly = 654321;
    input.rx = -777;
    input.ry = 888;
    input.leftTrigger = 0x12345678u;
    input.rightTrigger = 0x87654321u;
    input.generation = 42;
    input.timestampUs = 1000000;

    const PassThroughMapping mapping;
    const LogicalGamepadState output = mapping.process(input);

    assert(output.connected);
    assert(output.buttons == input.buttons);
    assert(output.dpad == input.dpad);
    assert(output.lx == input.lx);
    assert(output.ly == input.ly);
    assert(output.rx == input.rx);
    assert(output.ry == input.ry);
    assert(output.leftTrigger == input.leftTrigger);
    assert(output.rightTrigger == input.rightTrigger);
    assert(output.generation == input.generation);
    assert(output.timestampUs == input.timestampUs);

    std::cout << "OAG_U1A_CORE_TESTS=PASS\n";
    return 0;
}
