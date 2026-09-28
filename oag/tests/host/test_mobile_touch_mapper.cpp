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
    LogicalGamepadState neutral {};
    neutral.connected = true;

    MobileTouchMapper mapper;
    assert(mapper.map(LogicalGamepadState {}).count == 0);
    assert(mapper.map(neutral).count == 0);

    LogicalGamepadState up {};
    up.connected = true;
    up.dpad = static_cast<std::uint8_t>(DpadBits::Up);

    assertPoint(mapper.map(up), 1, 10316, 6394);
    assertPoint(mapper.map(up), 1, 12743, 6394);
    assertPoint(mapper.map(up), 1, 12743, 6394);
    assert(findContact(mapper.map(neutral), 1) == nullptr);

    MobileTouchMapper cameraMapper;
    LogicalGamepadState lookRight {};
    lookRight.connected = true;
    lookRight.rx =
        std::numeric_limits<std::int32_t>::max();

    assertPoint(cameraMapper.map(lookRight), 0, 16384, 17368);

    LogicalGamepadState cameraFire {};
    cameraFire.connected = true;
    cameraFire.buttons =
        kPubgMouseLookHoldButton |
        kPubgMouseLeftButton;

    const auto cameraAndFire = cameraMapper.map(cameraFire);
    assert(cameraAndFire.count == 2);
    assertPoint(cameraAndFire, 0, 16384, 17368);
    assertPoint(cameraAndFire, 2, 16869, 31451);

    MobileTouchMapper fireMapper;
    LogicalGamepadState fire {};
    fire.connected = true;
    fire.buttons = kPubgMouseLeftButton;
    const auto fireOnly = fireMapper.map(fire);
    assert(fireOnly.count == 1);
    assert(findContact(fireOnly, 0) == nullptr);
    assertPoint(fireOnly, 2, 16869, 31451);

    MobileTouchMapper edgeMapper;
    assertPoint(edgeMapper.map(lookRight), 0, 16384, 17368);
    assertPoint(edgeMapper.map(lookRight), 0, 16384, 18352);
    assertPoint(edgeMapper.map(lookRight), 0, 16384, 19336);
    assert(findContact(edgeMapper.map(lookRight), 0) == nullptr);
    assertPoint(edgeMapper.map(lookRight), 0, 16384, 17368);

    MobileTouchMapper actions;

    LogicalGamepadState jump {};
    jump.connected = true;
    jump.buttons = ButtonSouth;
    assertPoint(actions.map(jump), 3, 10407, 30600);

    LogicalGamepadState f {};
    f.connected = true;
    f.buttons = ButtonWest;
    assertPoint(actions.map(f), 4, 20632, 22367);

    LogicalGamepadState triShort {};
    triShort.connected = true;
    triShort.buttons = kPubgTriangleShortButton;
    assertPoint(actions.map(triShort), 5, 2124, 27663);

    LogicalGamepadState triHold {};
    triHold.connected = true;
    triHold.buttons = kPubgTriangleHoldButton;
    assertPoint(actions.map(triHold), 6, 2427, 29420);

    LogicalGamepadState r1 {};
    r1.connected = true;
    r1.buttons = ButtonRightBumper;
    assertPoint(actions.map(r1), 7, 20935, 6998);

    LogicalGamepadState l1 {};
    l1.connected = true;
    l1.buttons = ButtonLeftBumper;
    assertPoint(actions.map(l1), 8, 20935, 4803);

    LogicalGamepadState share {};
    share.connected = true;
    share.buttons = ButtonShare;
    assertPoint(actions.map(share), 9, 3034, 2950);

    LogicalGamepadState shift {};
    shift.connected = true;
    shift.buttons = kPubgShiftButton;
    assertPoint(actions.map(shift), 10, 26093, 26415);

    LogicalGamepadState rightClick {};
    rightClick.connected = true;
    rightClick.buttons = kPubgMouseRightButton;
    assertPoint(actions.map(rightClick), 11, 15929, 30600);

    LogicalGamepadState r {};
    r.connected = true;
    r.buttons = kPubgKeyRButton;
    assertPoint(actions.map(r), 12, 2336, 25386);

    LogicalGamepadState scrollDown {};
    scrollDown.connected = true;
    scrollDown.buttons = kPubgScrollDownButton;
    assertPoint(actions.map(scrollDown), 13, 3034, 14408);

    LogicalGamepadState scrollUp {};
    scrollUp.connected = true;
    scrollUp.buttons = kPubgScrollUpButton;
    assertPoint(actions.map(scrollUp), 14, 3034, 17907);

    LogicalGamepadState middle {};
    middle.connected = true;
    middle.buttons = kPubgMouseMiddleButton;
    assertPoint(actions.map(middle), 15, 2488, 21612);

    LogicalGamepadState g {};
    g.connected = true;
    g.buttons = kPubgKeyGButton;
    assertPoint(actions.map(g), 16, 2731, 10978);

    MobileTouchMapper comboMapper;

    LogicalGamepadState prime {};
    prime.connected = true;
    prime.dpad = static_cast<std::uint8_t>(DpadBits::Up);
    prime.rx = std::numeric_limits<std::int32_t>::max();
    const auto primeFrame = comboMapper.map(prime);
    assertPoint(primeFrame, 0, 16384, 17368);
    assertPoint(primeFrame, 1, 10316, 6394);

    LogicalGamepadState combo {};
    combo.connected = true;
    combo.dpad = static_cast<std::uint8_t>(DpadBits::Up);
    combo.buttons =
        kPubgMouseLookHoldButton |
        kPubgMouseLeftButton |
        kPubgMouseRightButton |
        kPubgShiftButton |
        kPubgKeyRButton |
        kPubgKeyGButton |
        ButtonWest;

    const auto comboFrame = comboMapper.map(combo);
    assert(comboFrame.count == 8);
    assertPoint(comboFrame, 0, 16384, 17368);
    assertPoint(comboFrame, 1, 12743, 6394);
    assertPoint(comboFrame, 2, 16869, 31451);
    assertPoint(comboFrame, 4, 20632, 22367);
    assertPoint(comboFrame, 10, 26093, 26415);
    assertPoint(comboFrame, 11, 15929, 30600);
    assertPoint(comboFrame, 12, 2336, 25386);
    assertPoint(comboFrame, 16, 2731, 10978);

    return 0;
}
