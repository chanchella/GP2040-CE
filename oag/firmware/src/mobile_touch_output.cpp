#include "oag/firmware/mobile_touch_output.h"

#include <cstddef>
#include <cstdint>

#include "tusb.h"

namespace oag::firmware {
namespace {

const oag::MobileTouchContact* findContactById(
    const oag::MobileTouchFrame& frame,
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
    std::uint8_t reportedContacts = 0;

    const auto appendReportContact =
        [&next, &reportedContacts](
            std::uint8_t flags,
            const oag::MobileTouchContact& contact
        ) {
            if (reportedContacts >= next.contacts.size()) {
                return;
            }

            ContactReport& out =
                next.contacts[reportedContacts++];

            out.flags = flags;
            out.id = contact.id;
            out.x = contact.x;
            out.y = contact.y;
        };

    // Report all currently active contacts first.
    for (std::uint8_t i = 0; i < frame.count; ++i) {
        appendReportContact(
            0x03u, // Tip Switch + In Range.
            frame.contacts[i]
        );
    }

    // A HID touch contact must keep the same Contact ID for its full
    // lifecycle and must be explicitly reported once with Tip Switch clear
    // when it leaves the surface. The old implementation dropped released
    // IDs from the packet and sent contactCount=0, which left Android
    // believing IDs 1/2/3 were still down. Contact ID 0 appeared to work only
    // because the zero-initialized first contact also happened to use ID 0.
    //
    // Preserve the last X/Y for the UP report, as required by HID touch
    // semantics, and count the released contact as a reported contact in this
    // packet even though its Tip Switch is clear.
    for (std::uint8_t i = 0; i < previousFrame_.count; ++i) {
        const oag::MobileTouchContact& previous =
            previousFrame_.contacts[i];

        if (findContactById(frame, previous.id) != nullptr) {
            continue;
        }

        appendReportContact(
            0x00u, // Tip Switch clear + Out of Range = explicit UP.
            previous
        );
    }

    next.contactCount = reportedContacts;

    report_ = next;
    previousFrame_ = frame;
    pending_ = true;
    flush();
    return true;
}

bool MobileTouchOutput::sendNeutral() {
    Report next {};
    std::uint8_t reportedContacts = 0;

    // If a profile switch or initialization path requests neutral while
    // contacts are active, emit their explicit UP transitions first.
    for (std::uint8_t i = 0; i < previousFrame_.count; ++i) {
        if (reportedContacts >= next.contacts.size()) {
            break;
        }

        const oag::MobileTouchContact& previous =
            previousFrame_.contacts[i];

        ContactReport& out =
            next.contacts[reportedContacts++];

        out.flags = 0x00u;
        out.id = previous.id;
        out.x = previous.x;
        out.y = previous.y;
    }

    next.contactCount = reportedContacts;

    report_ = next;
    previousFrame_ = {};
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
