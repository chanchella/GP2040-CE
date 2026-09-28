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

    // WASD joystick lifecycle: CENTER -> DRAG -> HOLD -> RELEASE.
    LogicalGamepadState up {};
    up.connected = true;
    up.dpad = static_cast<std::uint8_t>(DpadBits::Up);

    const auto upCenter = mapper.map(up);
    assert(upCenter.count == 1);
    assertPoint(upCenter, 1, 10316, 6394);

    const auto upDragged = mapper.map(up);
    assert(upDragged.count == 1);
    assertPoint(upDragged, 1, 12743, 6394);

    const auto upHeld = mapper.map(up);
    assertPoint(upHeld, 1, 12743, 6394);

    const auto movementReleased = mapper.map(neutral);
    assert(findContact(movementReleased, 1) == nullptr);

    // Restart must press center again before dragging.
    const auto restartCenter = mapper.map(up);
    assertPoint(restartCenter, 1, 10316, 6394);

    // Diagonal W+D uses the combined measured endpoint.
    LogicalGamepadState upRight {};
    upRight.connected = true;
    upRight.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Right);

    // Existing movement contact changes direction without re-pressing center.
    const auto diagonal = mapper.map(upRight);
    assertPoint(diagonal, 1, 12743, 7684);

    // Opposing axes cancel independently.
    MobileTouchMapper conflictMapper;
    LogicalGamepadState conflict {};
    conflict.connected = true;
    conflict.dpad =
        static_cast<std::uint8_t>(DpadBits::Up) |
        static_cast<std::uint8_t>(DpadBits::Down);
    assert(findContact(conflictMapper.map(conflict), 1) == nullptr);

    // Camera movement starts independently on contact 0.
    MobileTouchMapper cameraMapper;
    LogicalGamepadState lookRight {};
    lookRight.connected = true;
    lookRight.rx =
        std::numeric_limits<std::int32_t>::max();

    const auto cameraMove = cameraMapper.map(lookRight);
    assertPoint(cameraMove, 0, 11302, 27056);

    // A fire report with ZERO mouse delta can hold the camera finger at the
    // exact same coordinate while fire uses a different contact ID.
    LogicalGamepadState fireWhileHoldingCamera {};
    fireWhileHoldingCamera.connected = true;
    fireWhileHoldingCamera.buttons =
        kPubgMouseLookHoldButton |
        kPubgMouseLeftButton;

    const auto cameraAndFire =
        cameraMapper.map(fireWhileHoldingCamera);

    assert(cameraAndFire.count == 2);
    assertPoint(cameraAndFire, 0, 11302, 27056);
    assertPoint(cameraAndFire, 2, 16869, 31451);

    // Fire alone never invents a camera touch.
    MobileTouchMapper fireMapper;
    LogicalGamepadState fire {};
    fire.connected = true;
    fire.buttons = kPubgMouseLeftButton;
    const auto fireOnly = fireMapper.map(fire);
    assert(fireOnly.count == 1);
    assert(findContact(fireOnly, 0) == nullptr);
    assertPoint(fireOnly, 2, 16869, 31451);

    // Camera remains bounded and re-anchors only after a clean release.
    MobileTouchMapper edgeMapper;
    assertPoint(edgeMapper.map(lookRight), 0, 11302, 27056);
    assertPoint(edgeMapper.map(lookRight), 0, 11302, 28040);
    assertPoint(edgeMapper.map(lookRight), 0, 11302, 29024);

    const auto edgeRelease = edgeMapper.map(lookRight);
    assert(findContact(edgeRelease, 0) == nullptr);

    assertPoint(edgeMapper.map(lookRight), 0, 11302, 27056);

    // Other PUBG targets remain direct and independent.
    MobileTouchMapper actions;

    LogicalGamepadState jump {};
    jump.connected = true;
    jump.buttons = ButtonSouth;
    assertPoint(actions.map(jump), 3, 10407, 30600);

    LogicalGamepadState square {};
    square.connected = true;
    square.buttons = ButtonWest;
    assertPoint(actions.map(square), 4, 6372, 28542);

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

    // Representative gameplay multitouch:
    // camera + movement + fire + jump + R1.
    MobileTouchMapper comboMapper;

    // Prime movement center + establish camera position.
    LogicalGamepadState prime {};
    prime.connected = true;
    prime.dpad = static_cast<std::uint8_t>(DpadBits::Up);
    prime.rx = std::numeric_limits<std::int32_t>::max();
    const auto primeFrame = comboMapper.map(prime);
    assertPoint(primeFrame, 0, 11302, 27056);
    assertPoint(primeFrame, 1, 10316, 6394);

    LogicalGamepadState combo {};
    combo.connected = true;
    combo.dpad = static_cast<std::uint8_t>(DpadBits::Up);
    combo.buttons =
        kPubgMouseLookHoldButton |
        kPubgMouseLeftButton |
        ButtonSouth |
        ButtonRightBumper;

    const auto comboFrame = comboMapper.map(combo);
    assert(comboFrame.count == 5);
    assertPoint(comboFrame, 0, 11302, 27056);
    assertPoint(comboFrame, 1, 12743, 6394);
    assertPoint(comboFrame, 2, 16869, 31451);
    assertPoint(comboFrame, 3, 10407, 30600);
    assertPoint(comboFrame, 7, 20935, 6998);

    return 0;
}
