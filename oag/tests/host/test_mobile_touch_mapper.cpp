#include <cassert>
#include <cstdint>

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

} // namespace

int main() {
    MobileTouchMapper mapper;

    // Disconnected and connected-neutral states must produce no contacts.
    assert(mapper.map(LogicalGamepadState {}).count == 0);

    LogicalGamepadState neutral {};
    neutral.connected = true;
    assert(mapper.map(neutral).count == 0);

    // South held => one persistent diagnostic contact at screen center.
    LogicalGamepadState press {};
    press.connected = true;
    press.buttons = ButtonSouth;

    const MobileTouchFrame pressed = mapper.map(press);
    assert(pressed.count == 1);

    const MobileTouchContact* center = findContact(pressed, 0);
    assert(center != nullptr);
    assert(center->x == 16384);
    assert(center->y == 16384);

    // Same input on the next frame must keep the same contact id/position,
    // allowing the firmware to represent a held finger.
    const MobileTouchFrame held = mapper.map(press);
    assert(held.count == 1);

    const MobileTouchContact* heldCenter = findContact(held, 0);
    assert(heldCenter != nullptr);
    assert(heldCenter->x == 16384);
    assert(heldCenter->y == 16384);

    // Release => empty frame. MobileTouchOutput then sends contactCount=0.
    LogicalGamepadState release {};
    release.connected = true;
    assert(mapper.map(release).count == 0);

    // All unrelated controls are intentionally ignored in this diagnostic.
    LogicalGamepadState unrelated {};
    unrelated.connected = true;
    unrelated.buttons =
        ButtonEast |
        ButtonWest |
        ButtonNorth |
        ButtonRightStick;
    unrelated.leftTrigger = 0xFFFFFFFFu;
    unrelated.rightTrigger = 0xFFFFFFFFu;
    unrelated.lx = 0x7FFFFFFF;
    unrelated.ly = -0x7FFFFFFF;

    assert(mapper.map(unrelated).count == 0);

    return 0;
}
