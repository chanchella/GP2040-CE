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

    // Previously verified left analog movement stays unchanged.
    LogicalGamepadState leftAnalog {};
    leftAnalog.connected = true;
    leftAnalog.lx = std::numeric_limits<std::int32_t>::min();
    assertPoint(mapper.map(leftAnalog), 0, 6796, 3527);

    LogicalGamepadState rightAnalog {};
    rightAnalog.connected = true;
    rightAnalog.lx = std::numeric_limits<std::int32_t>::max();
    assertPoint(mapper.map(rightAnalog), 0, 6341, 9358);

    LogicalGamepadState r3 {};
    r3.connected = true;
    r3.buttons = ButtonRightStick;
    assertPoint(mapper.map(r3), 0, 6614, 6243);

    // New jump target.
    LogicalGamepadState cross {};
    cross.connected = true;
    cross.buttons = ButtonSouth;
    assertPoint(mapper.map(cross), 1, 10407, 30600);

    // Square and physical R2 stay at their verified targets.
    LogicalGamepadState square {};
    square.connected = true;
    square.buttons = ButtonWest;
    assertPoint(mapper.map(square), 2, 6372, 28542);

    LogicalGamepadState r2 {};
    r2.connected = true;
    r2.rightTrigger = 0xFFFFFFFFu;
    assertPoint(mapper.map(r2), 3, 13805, 28404);

    // Mouse look starts at the measured box center and moves at exactly half
    // the V5 step size. Full +X means landscape-right => HID Y increases.
    MobileTouchMapper lookMapper;
    LogicalGamepadState lookRight {};
    lookRight.connected = true;
    lookRight.rx = std::numeric_limits<std::int32_t>::max();

    const auto look1 = lookMapper.map(lookRight);
    assertPoint(look1, 4, 11302, 27056);

    const auto look2 = lookMapper.map(lookRight);
    assertPoint(look2, 4, 11302, 28040);

    const auto look3 = lookMapper.map(lookRight);
    assertPoint(look3, 4, 11302, 29024);

    // Next step would exceed Y=29639, so the contact is released rather than
    // ever leaving the user's rectangle.
    const auto edgeRelease = lookMapper.map(lookRight);
    assert(findContact(edgeRelease, 4) == nullptr);

    const auto edgeRestart = lookMapper.map(lookRight);
    assertPoint(edgeRestart, 4, 11302, 27056);

    // Full mouse-down means natural portrait HID X decreases.
    MobileTouchMapper lookDownMapper;
    LogicalGamepadState lookDown {};
    lookDown.connected = true;
    lookDown.ry = std::numeric_limits<std::int32_t>::max();
    assertPoint(
        lookDownMapper.map(lookDown),
        4,
        9118,
        26072
    );

    // Hold bit keeps the current look finger exactly stationary.
    LogicalGamepadState holdLook {};
    holdLook.connected = true;
    holdLook.buttons = kMobileTouchMouseLookHoldButton;
    assertPoint(
        lookDownMapper.map(holdLook),
        4,
        9118,
        26072
    );

    assert(findContact(lookDownMapper.map(neutral), 4) == nullptr);

    // Mouse-left has an independent touchscreen target.
    LogicalGamepadState mouseLeft {};
    mouseLeft.connected = true;
    mouseLeft.buttons = kMobileTouchMouseLeftButton;
    assertPoint(mapper.map(mouseLeft), 5, 16869, 31451);

    // Triangle short and hold use separate synthetic actions so firmware can
    // enforce the 50 ms / 100 ms timing without confusing touch identities.
    LogicalGamepadState triShort {};
    triShort.connected = true;
    triShort.buttons = kMobileTouchTriangleShortButton;
    assertPoint(mapper.map(triShort), 6, 2124, 27663);

    LogicalGamepadState triHold {};
    triHold.connected = true;
    triHold.buttons = kMobileTouchTriangleHoldButton;
    assertPoint(mapper.map(triHold), 7, 2427, 29420);

    LogicalGamepadState share {};
    share.connected = true;
    share.buttons = ButtonShare;
    assertPoint(mapper.map(share), 8, 3034, 2950);

    LogicalGamepadState legacyShare {};
    legacyShare.connected = true;
    legacyShare.buttons = ButtonBack;
    assertPoint(mapper.map(legacyShare), 8, 3034, 2950);

    LogicalGamepadState r1 {};
    r1.connected = true;
    r1.buttons = ButtonRightBumper;
    assertPoint(mapper.map(r1), 9, 20935, 6998);

    LogicalGamepadState l1 {};
    l1.connected = true;
    l1.buttons = ButtonLeftBumper;
    assertPoint(mapper.map(l1), 10, 20935, 4803);

    LogicalGamepadState dpad {};
    dpad.connected = true;
    dpad.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Right);

    const auto dpadFrame = mapper.map(dpad);
    assert(dpadFrame.count == 2);
    assertPoint(dpadFrame, 11, 12743, 6394);
    assertPoint(dpadFrame, 14, 10316, 7684);

    LogicalGamepadState dpadDown {};
    dpadDown.connected = true;
    dpadDown.dpad = static_cast<std::uint8_t>(DpadBits::Down);
    assertPoint(mapper.map(dpadDown), 12, 6372, 6394);

    LogicalGamepadState dpadLeft {};
    dpadLeft.connected = true;
    dpadLeft.dpad = static_cast<std::uint8_t>(DpadBits::Left);
    assertPoint(mapper.map(dpadLeft), 13, 10316, 5214);

    // Core simultaneous behavior: movement + jump + physical R2 + mouse-look
    // + independent mouse-left must all coexist as separate contacts.
    MobileTouchMapper multiMapper;
    LogicalGamepadState all {};
    all.connected = true;
    all.lx = std::numeric_limits<std::int32_t>::max();
    all.buttons =
        ButtonSouth |
        kMobileTouchMouseLeftButton;
    all.rightTrigger = 0xFFFFFFFFu;
    all.rx = std::numeric_limits<std::int32_t>::max();

    const auto allFrame = multiMapper.map(all);
    assert(allFrame.count == 5);
    assertPoint(allFrame, 0, 6341, 9358);
    assertPoint(allFrame, 1, 10407, 30600);
    assertPoint(allFrame, 3, 13805, 28404);
    assertPoint(allFrame, 4, 11302, 27056);
    assertPoint(allFrame, 5, 16869, 31451);

    return 0;
}
