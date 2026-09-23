#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "oag/core/product_identity.h"
#include "oag/device/device_registry.h"
#include "oag/input/gamepad_state.h"
#include "oag/mapping/logical_slot_manager.h"
#include "oag/mapping/pass_through_mapping.h"
#include "oag/protocol/xusb/xusb_input_driver.h"
#include "oag/transport/host_root_reconciler.h"

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

    const std::uint8_t xusbReport[20] = {
        0x00, 0x14,
        0x11, 0x14, // Up + Start + Guide + South(A)
        0xFF, 0x80, // LT max, RT mid
        0x00, 0x80, // LX = -32768
        0xFF, 0x7F, // LY = +32767 (XUSB Up)
        0x00, 0x00, // RX = 0
        0x01, 0x00, // RY = +1 (XUSB Up)
        0, 0, 0, 0, 0, 0
    };

    UniversalGamepadState input {};
    const XusbInputDriver xusb;
    assert(xusb.parse(*second, xusbReport, sizeof(xusbReport), 1000000, input));

    assert(input.connected);
    assert(input.source == *second);
    assert(input.dpad & static_cast<std::uint8_t>(DpadBits::Up));
    assert(input.buttons & ButtonStart);
    assert(input.buttons & ButtonGuide);
    assert(input.buttons & ButtonSouth);
    assert(input.leftTrigger == std::numeric_limits<std::uint32_t>::max());
    assert(input.rightTrigger == 0x80808080u);
    assert(input.lx == std::numeric_limits<std::int32_t>::min());
    assert(input.ly == -std::numeric_limits<std::int32_t>::max());
    assert(input.rx == 0);
    assert(input.ry < 0);

    UniversalGamepadState yDown {};
    std::uint8_t yDownReport[20] = {
        0x00, 0x14,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x80, // LY = -32768 (XUSB Down)
        0x00, 0x00,
        0x00, 0x80, // RY = -32768 (XUSB Down)
        0, 0, 0, 0, 0, 0
    };
    assert(xusb.parse(*second, yDownReport, sizeof(yDownReport), 1000100, yDown));
    assert(yDown.ly == std::numeric_limits<std::int32_t>::max());
    assert(yDown.ry == std::numeric_limits<std::int32_t>::max());

    UniversalGamepadState rejected {};
    std::uint8_t invalidReport[20] = {};
    invalidReport[1] = 0x13;
    assert(!xusb.parse(*second, invalidReport, sizeof(invalidReport), 0, rejected));

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
