#include "addons/universal_output_router.h"

#include "output/universal_output_manager.h"

bool UniversalOutputRouterAddon::available() {
    return UNIVERSAL_OUTPUT_ROUTER_ENABLED;
}

void UniversalOutputRouterAddon::setup() {
    UOUTPUT.resetAll();
}

void UniversalOutputRouterAddon::preprocess() {
    // G2C: deterministic 1:1 routing only.
    //
    // Input Slot N -> Logical Output Slot N.
    //
    // Physical/platform USB drivers consume UniversalOutputManager directly.
    // No slot is merged into the legacy single gamepad state here.
    UOUTPUT.syncFromInputs();
}
