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

} // namespace

int main() {
    MobileTouchMapper mapper;

    const MobileTouchFrame neutral =
        mapper.map(LogicalGamepadState {});
    assert(neutral.count == 0);

    LogicalGamepadState state {};
    state.connected = true;
    state.buttons =
        ButtonSouth |
        ButtonNorth |
        ButtonRightBumper |
        ButtonStart;
    state.leftTrigger = 0xFFFFFFFFu;
    state.lx = std::numeric_limits<std::int32_t>::max();
    state.ly = 0;

    const MobileTouchFrame frame = mapper.map(state);

    assert(findContact(frame, 0) != nullptr);
    assert(findContact(frame, 2) != nullptr);
    assert(findContact(frame, 5) != nullptr);
    assert(findContact(frame, 7) != nullptr);
    assert(findContact(frame, 8) != nullptr);
    assert(findContact(frame, 10) != nullptr);

    const MobileTouchContact* move =
        findContact(frame, 0);
    assert(move != nullptr);
    assert(move->x > 6500);
    assert(move->y == 24500);

    LogicalGamepadState dpad {};
    dpad.connected = true;
    dpad.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Left);

    const MobileTouchFrame dpadFrame = mapper.map(dpad);
    const MobileTouchContact* dpadContact =
        findContact(dpadFrame, 0);

    assert(dpadContact != nullptr);
    assert(dpadContact->x < 6500);
    assert(dpadContact->y < 24500);

    LogicalGamepadState crowded {};
    crowded.connected = true;
    crowded.lx = std::numeric_limits<std::int32_t>::max();
    crowded.rx = std::numeric_limits<std::int32_t>::max();
    crowded.buttons =
        ButtonSouth |
        ButtonEast |
        ButtonWest |
        ButtonNorth |
        ButtonLeftBumper |
        ButtonRightBumper |
        ButtonStart |
        ButtonBack |
        ButtonLeftStick |
        ButtonRightStick |
        ButtonGuide |
        ButtonShare;
    crowded.leftTrigger = 0xFFFFFFFFu;
    crowded.rightTrigger = 0xFFFFFFFFu;

    const MobileTouchFrame crowdedFrame =
        mapper.map(crowded);
    assert(crowdedFrame.count == MobileTouchFrame::kMaxContacts);

    return 0;
}
