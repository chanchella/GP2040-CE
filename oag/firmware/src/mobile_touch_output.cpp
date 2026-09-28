#include "oag/firmware/mobile_touch_output.h"

#include <cstddef>
#include <cstdint>

#include "tusb.h"

namespace oag::firmware {

void MobileTouchOutput::task() {
    flush();
}

bool MobileTouchOutput::send(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    // A phone touch surface is a single-user target. The existing routing
    // always places the selected Primary controller in output slot 0 for
    // non-XInput profiles; ignore secondary output slots instead of merging
    // unrelated players onto one touchscreen.
    if (logicalSlot != 0) {
        return true;
    }

    const oag::MobileTouchFrame frame =
        mapper_.map(state);

    Report next {};
    next.contactCount = frame.count;

    for (std::uint8_t i = 0; i < frame.count; ++i) {
        next.contacts[i].flags = 0x03u; // Tip Switch + In Range.
        next.contacts[i].id = frame.contacts[i].id;
        next.contacts[i].x = frame.contacts[i].x;
        next.contacts[i].y = frame.contacts[i].y;
    }

    report_ = next;
    pending_ = true;
    flush();
    return true;
}

bool MobileTouchOutput::sendNeutral() {
    report_ = {};
    pending_ = true;
    flush();
    return true;
}

bool MobileTouchOutput::flush() {
    if (!pending_) {
        return true;
    }

    if (!tud_hid_n_ready(0)) {
        return false;
    }

    if (!tud_hid_n_report(
            0,
            0,
            &report_,
            sizeof(report_)
        )) {
        return false;
    }

    pending_ = false;
    return true;
}

} // namespace oag::firmware
