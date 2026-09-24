#include <cassert>

#include "oag/input/mouse_state.h"
#include "oag/mapping/pen_tablet_mapper.h"
#include "oag/mapping/touchscreen_mapper.h"
#include "oag/output/universal_output_mode.h"

using namespace oag;

int main() {
    UniversalOutputModeRouter router;

    auto routing = router.routing();
    assert(routing.gamepad);
    assert(!routing.touchscreen);
    assert(!routing.penTablet);

    router.setMode(UniversalOutputMode::Touchscreen);
    routing = router.routing();
    assert(!routing.gamepad);
    assert(routing.touchscreen);
    assert(!routing.penTablet);

    router.setMode(UniversalOutputMode::DrawingTablet);
    routing = router.routing();
    assert(routing.penTablet);
    assert(!routing.touchscreen);

    router.setMode(UniversalOutputMode::Hybrid);
    routing = router.routing();
    assert(routing.gamepad);
    assert(routing.touchscreen);
    assert(routing.penTablet);
    assert(routing.keyboard);
    assert(routing.mouse);
    assert(routing.bluetoothPeripheral);

    MouseState mouse {};
    mouse.connected = true;
    mouse.buttons = MouseButtonLeft | MouseButtonRight;
    mouse.timestampUs = 1234;

    TouchscreenMapper touch;
    const TouchDigitizerState touchState =
        touch.apply(mouse, {10, -5});

    assert(touchState.connected);
    assert(touchState.inRange);
    assert(touchState.contact);
    assert(touchState.x > 16384);
    assert(touchState.y < 16384);

    PenTabletMapper pen;
    const PenDigitizerState penState =
        pen.apply(mouse, {10, -5});

    assert(penState.connected);
    assert(penState.tip);
    assert(penState.barrel);
    assert(!penState.eraser);
    assert(penState.pressure == PenTabletMapper::kPressureMax);

    return 0;
}
