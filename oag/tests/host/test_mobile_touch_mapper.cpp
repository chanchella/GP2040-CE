#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/input/gamepad_state.h"
#include "oag/output/touch/mobile_touch_mapper.h"

using namespace oag;

namespace {

const MobileTouchContact* findContact(
    const MobileTouchFrame& frame,
    std::uint8_t id
) {
    for (std::uint8_t i = 0; i < frame.count; ++i) {
        if (frame.contacts[i].id == id) {
            return &frame.contacts[i];
        }
    }

    return nullptr;
}

void assertPoint(
    const MobileTouchFrame& frame,
    std::uint8_t id,
    std::uint16_t x,
    std::uint16_t y
) {
    const MobileTouchContact* c = findContact(frame, id);
    assert(c != nullptr);
    assert(c->x == x);
    assert(c->y == y);
}

} // namespace

int main() {
    MobileTouchMapper mapper;

    const MobileTouchFrame disconnected =
        mapper.map(LogicalGamepadState {});
    assert(disconnected.count == 0);

    LogicalGamepadState connected {};
    connected.connected = true;
    assert(mapper.map(connected).count == 0);

    LogicalGamepadState left {};
    left.connected = true;
    left.dpad = static_cast<std::uint8_t>(DpadBits::Left);
    const MobileTouchFrame leftFrame = mapper.map(left);
    assert(leftFrame.count == 1);
    assertPoint(leftFrame, 0, 3139, 25903);

    LogicalGamepadState leftStick {};
    leftStick.connected = true;
    leftStick.lx = std::numeric_limits<std::int32_t>::min();
    const MobileTouchFrame leftStickFrame = mapper.map(leftStick);
    assert(leftStickFrame.count == 1);
    assertPoint(leftStickFrame, 0, 3139, 25903);

    LogicalGamepadState right {};
    right.connected = true;
    right.dpad = static_cast<std::uint8_t>(DpadBits::Right);
    const MobileTouchFrame rightFrame = mapper.map(right);
    assert(rightFrame.count == 1);
    assertPoint(rightFrame, 0, 8681, 25941);

    LogicalGamepadState rightStick {};
    rightStick.connected = true;
    rightStick.lx = std::numeric_limits<std::int32_t>::max();
    const MobileTouchFrame rightStickFrame = mapper.map(rightStick);
    assert(rightStickFrame.count == 1);
    assertPoint(rightStickFrame, 0, 8681, 25941);

    LogicalGamepadState r3 {};
    r3.connected = true;
    r3.buttons = ButtonRightStick;
    const MobileTouchFrame r3Frame = mapper.map(r3);
    assert(r3Frame.count == 1);
    assertPoint(r3Frame, 0, 6107, 25827);

    LogicalGamepadState cross {};
    cross.connected = true;
    cross.buttons = ButtonSouth;
    const MobileTouchFrame crossFrame = mapper.map(cross);
    assert(crossFrame.count == 1);
    assertPoint(crossFrame, 1, 28547, 26775);

    LogicalGamepadState mouseLeftEquivalent {};
    mouseLeftEquivalent.connected = true;
    mouseLeftEquivalent.rightTrigger = 0xFFFFFFFFu;
    const MobileTouchFrame mouseFrame =
        mapper.map(mouseLeftEquivalent);
    assert(mouseFrame.count == 1);
    assertPoint(mouseFrame, 1, 28547, 26775);

    // Movement + attack must remain true multitouch.
    LogicalGamepadState combo {};
    combo.connected = true;
    combo.lx = std::numeric_limits<std::int32_t>::max();
    combo.buttons = ButtonSouth;
    const MobileTouchFrame comboFrame = mapper.map(combo);
    assert(comboFrame.count == 2);
    assertPoint(comboFrame, 0, 8681, 25941);
    assertPoint(comboFrame, 1, 28547, 26775);

    // R3 owns the rocker contact even if a movement direction is also held.
    LogicalGamepadState r3Priority {};
    r3Priority.connected = true;
    r3Priority.buttons = ButtonRightStick;
    r3Priority.lx = std::numeric_limits<std::int32_t>::max();
    const MobileTouchFrame r3PriorityFrame =
        mapper.map(r3Priority);
    assert(r3PriorityFrame.count == 1);
    assertPoint(r3PriorityFrame, 0, 6107, 25827);

    // Opposing directions cancel instead of creating an ambiguous touch.
    LogicalGamepadState conflict {};
    conflict.connected = true;
    conflict.dpad =
        static_cast<std::uint8_t>(DpadBits::Left) |
        static_cast<std::uint8_t>(DpadBits::Right);
    assert(mapper.map(conflict).count == 0);

    return 0;
}
