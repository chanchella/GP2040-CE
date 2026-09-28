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

    assert(mapper.map(LogicalGamepadState {}).count == 0);

    LogicalGamepadState neutral {};
    neutral.connected = true;
    assert(mapper.map(neutral).count == 0);

    LogicalGamepadState left {};
    left.connected = true;
    left.dpad = static_cast<std::uint8_t>(DpadBits::Left);
    const auto leftFrame = mapper.map(left);
    assert(leftFrame.count == 1);
    assertPoint(leftFrame, 0, 3139, 25903);

    LogicalGamepadState leftAnalog {};
    leftAnalog.connected = true;
    leftAnalog.lx = std::numeric_limits<std::int32_t>::min();
    const auto leftAnalogFrame = mapper.map(leftAnalog);
    assert(leftAnalogFrame.count == 1);
    assertPoint(leftAnalogFrame, 0, 3139, 25903);

    LogicalGamepadState right {};
    right.connected = true;
    right.dpad = static_cast<std::uint8_t>(DpadBits::Right);
    const auto rightFrame = mapper.map(right);
    assert(rightFrame.count == 1);
    assertPoint(rightFrame, 0, 8681, 25941);

    LogicalGamepadState rightAnalog {};
    rightAnalog.connected = true;
    rightAnalog.lx = std::numeric_limits<std::int32_t>::max();
    const auto rightAnalogFrame = mapper.map(rightAnalog);
    assert(rightAnalogFrame.count == 1);
    assertPoint(rightAnalogFrame, 0, 8681, 25941);

    LogicalGamepadState r3 {};
    r3.connected = true;
    r3.buttons = ButtonRightStick;
    const auto r3Frame = mapper.map(r3);
    assert(r3Frame.count == 1);
    assertPoint(r3Frame, 0, 6107, 25827);

    LogicalGamepadState cross {};
    cross.connected = true;
    cross.buttons = ButtonSouth;
    const auto crossFrame = mapper.map(cross);
    assert(crossFrame.count == 1);
    assertPoint(crossFrame, 1, 30897, 21010);

    LogicalGamepadState square {};
    square.connected = true;
    square.buttons = ButtonWest;
    const auto squareFrame = mapper.map(square);
    assert(squareFrame.count == 1);
    assertPoint(squareFrame, 2, 28272, 26965);

    LogicalGamepadState r2 {};
    r2.connected = true;
    r2.rightTrigger = 0xFFFFFFFFu;
    const auto r2Frame = mapper.map(r2);
    assert(r2Frame.count == 1);
    assertPoint(r2Frame, 3, 28135, 18394);

    // Real gameplay combinations must remain true multitouch.
    LogicalGamepadState combo {};
    combo.connected = true;
    combo.lx = std::numeric_limits<std::int32_t>::max();
    combo.buttons = ButtonSouth | ButtonWest;
    combo.rightTrigger = 0xFFFFFFFFu;

    const auto comboFrame = mapper.map(combo);
    assert(comboFrame.count == 4);
    assertPoint(comboFrame, 0, 8681, 25941);
    assertPoint(comboFrame, 1, 30897, 21010);
    assertPoint(comboFrame, 2, 28272, 26965);
    assertPoint(comboFrame, 3, 28135, 18394);

    // R3 exclusively owns the rocker finger even while a direction is held.
    LogicalGamepadState r3Priority {};
    r3Priority.connected = true;
    r3Priority.buttons = ButtonRightStick;
    r3Priority.lx = std::numeric_limits<std::int32_t>::max();

    const auto r3PriorityFrame = mapper.map(r3Priority);
    assert(r3PriorityFrame.count == 1);
    assertPoint(r3PriorityFrame, 0, 6107, 25827);

    // Opposing directions cancel rather than generating an ambiguous rocker
    // touch.
    LogicalGamepadState conflict {};
    conflict.connected = true;
    conflict.dpad =
        static_cast<std::uint8_t>(DpadBits::Left) |
        static_cast<std::uint8_t>(DpadBits::Right);

    assert(mapper.map(conflict).count == 0);

    return 0;
}
