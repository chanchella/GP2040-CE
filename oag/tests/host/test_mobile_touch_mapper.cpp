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

    LogicalGamepadState mouseLeft {};
    mouseLeft.connected = true;
    mouseLeft.buttons = kPubgMouseLeftButton;
    assertPoint(mapper.map(mouseLeft), 1, 16869, 31451);

    LogicalGamepadState jump {};
    jump.connected = true;
    jump.buttons = ButtonSouth;
    assertPoint(mapper.map(jump), 2, 10407, 30600);

    LogicalGamepadState square {};
    square.connected = true;
    square.buttons = ButtonWest;
    assertPoint(mapper.map(square), 3, 6372, 28542);

    LogicalGamepadState triShort {};
    triShort.connected = true;
    triShort.buttons = kPubgTriangleShortButton;
    assertPoint(mapper.map(triShort), 4, 2124, 27663);

    LogicalGamepadState triHold {};
    triHold.connected = true;
    triHold.buttons = kPubgTriangleHoldButton;
    assertPoint(mapper.map(triHold), 5, 2427, 29420);

    LogicalGamepadState r1 {};
    r1.connected = true;
    r1.buttons = ButtonRightBumper;
    assertPoint(mapper.map(r1), 6, 20935, 6998);

    LogicalGamepadState l1 {};
    l1.connected = true;
    l1.buttons = ButtonLeftBumper;
    assertPoint(mapper.map(l1), 7, 20935, 4803);

    LogicalGamepadState share {};
    share.connected = true;
    share.buttons = ButtonShare;
    assertPoint(mapper.map(share), 8, 3034, 2950);

    LogicalGamepadState directions {};
    directions.connected = true;
    directions.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Right);

    const auto dirFrame = mapper.map(directions);
    assert(dirFrame.count == 2);
    assertPoint(dirFrame, 9, 12743, 6394);
    assertPoint(dirFrame, 12, 10316, 7684);

    LogicalGamepadState down {};
    down.connected = true;
    down.dpad =
        static_cast<std::uint8_t>(DpadBits::Down);
    assertPoint(mapper.map(down), 10, 6372, 6394);

    LogicalGamepadState left {};
    left.connected = true;
    left.dpad =
        static_cast<std::uint8_t>(DpadBits::Left);
    assertPoint(mapper.map(left), 11, 10316, 5214);

    // Mouse look: bounded to the measured box and exactly half V5 speed.
    MobileTouchMapper lookMapper;

    LogicalGamepadState lookRight {};
    lookRight.connected = true;
    lookRight.rx =
        std::numeric_limits<std::int32_t>::max();

    assertPoint(
        lookMapper.map(lookRight),
        0,
        11302,
        27056
    );

    assertPoint(
        lookMapper.map(lookRight),
        0,
        11302,
        28040
    );

    assertPoint(
        lookMapper.map(lookRight),
        0,
        11302,
        29024
    );

    // The next step would leave Y=29639, so contact 0 is released.
    const auto edgeRelease =
        lookMapper.map(lookRight);
    assert(findContact(edgeRelease, 0) == nullptr);

    // Next movement re-anchors at the box center.
    assertPoint(
        lookMapper.map(lookRight),
        0,
        11302,
        27056
    );

    MobileTouchMapper downLookMapper;
    LogicalGamepadState lookDown {};
    lookDown.connected = true;
    lookDown.ry =
        std::numeric_limits<std::int32_t>::max();

    assertPoint(
        downLookMapper.map(lookDown),
        0,
        9118,
        26072
    );

    // Representative PUBG multitouch combination.
    MobileTouchMapper multiMapper;
    LogicalGamepadState combo {};
    combo.connected = true;
    combo.buttons =
        kPubgMouseLeftButton |
        ButtonSouth |
        ButtonRightBumper;
    combo.dpad =
        static_cast<std::uint8_t>(DpadBits::Up);
    combo.rx =
        std::numeric_limits<std::int32_t>::max();

    const auto comboFrame = multiMapper.map(combo);
    assert(comboFrame.count == 5);
    assertPoint(comboFrame, 0, 11302, 27056);
    assertPoint(comboFrame, 1, 16869, 31451);
    assertPoint(comboFrame, 2, 10407, 30600);
    assertPoint(comboFrame, 6, 20935, 6998);
    assertPoint(comboFrame, 9, 12743, 6394);

    return 0;
}
