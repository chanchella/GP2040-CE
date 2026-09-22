#include "addons/universal_output_router.h"

#include "output/universal_output_manager.h"
#include "storagemanager.h"

bool UniversalOutputRouterAddon::available() {
    return UNIVERSAL_OUTPUT_ROUTER_ENABLED;
}

void UniversalOutputRouterAddon::setup() {
    UOUTPUT.resetAll();
}

void UniversalOutputRouterAddon::preprocess() {
    // G2B: keep the logical output layer synchronized with the
    // global input slots using a deterministic 1:1 mapping.
    UOUTPUT.syncFromInputs();

    // Compatibility bridge for GP2040's current single USB-device
    // XInput implementation.
    //
    // Logical Output 1 corresponds to USB Input Slot 1 (T29).
    // No arbitration and no merging is performed here.
    //
    // G2C will replace this single bridge with a multi-instance
    // USB-device output implementation.
    UniversalOutputSlotSnapshot output {};
    if (
        !UOUTPUT.snapshot(UNIVERSAL_OUTPUT_SLOT_1, output) ||
        !output.connected ||
        !output.hasReport
    ) {
        return;
    }

    Gamepad* gamepad = Storage::getInstance().GetGamepad();

    gamepad->hasAnalogTriggers = true;
    gamepad->hasLeftAnalogStick = true;
    gamepad->hasRightAnalogStick = true;

    gamepad->state = output.state;
}
