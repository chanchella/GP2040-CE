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

void appendFrameContact(
    oag::MobileTouchFrame& frame,
    const oag::MobileTouchContact& contact
) {
    if (frame.count >= frame.contacts.size()) {
        return;
    }

    frame.contacts[frame.count++] = contact;
}

} // namespace

bool MobileTouchOutput::framesEqual(
    const oag::MobileTouchFrame& a,
    const oag::MobileTouchFrame& b
) {
    if (a.count != b.count) {
        return false;
    }

    for (std::uint8_t i = 0; i < a.count; ++i) {
        if (
            a.contacts[i].id != b.contacts[i].id ||
            a.contacts[i].x != b.contacts[i].x ||
            a.contacts[i].y != b.contacts[i].y
        ) {
            return false;
        }
    }

    return true;
}

void MobileTouchOutput::task() {
    pump();
}

bool MobileTouchOutput::send(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    // Profile 7 is one touchscreen. Only Primary/output slot 0 owns it.
    if (logicalSlot != 0) {
        return true;
    }

    desiredFrame_ = mapper_.map(state);
    pump();
    return true;
}

bool MobileTouchOutput::sendNeutral() {
    // Run the mapper through a disconnected state too, so its stateful camera
    // and movement gesture machines are reset together with the USB contacts.
    desiredFrame_ = mapper_.map(LogicalGamepadState {});
    pump();
    return true;
}

void MobileTouchOutput::preparePendingTransition() {
    report_ = {};
    pendingTargetFrame_ = {};

    std::uint8_t releaseCount = 0;

    for (std::uint8_t i = 0; i < committedFrame_.count; ++i) {
        if (
            findContactById(
                desiredFrame_,
                committedFrame_.contacts[i].id
            ) == nullptr
        ) {
            ++releaseCount;
        }
    }

    const bool needsBridge =
        static_cast<std::size_t>(desiredFrame_.count) +
            static_cast<std::size_t>(releaseCount) >
        kMaxContacts;

    std::uint8_t reportedContacts = 0;

    const auto appendReportContact =
        [this, &reportedContacts](
            std::uint8_t flags,
            const oag::MobileTouchContact& contact
        ) {
            if (reportedContacts >= report_.contacts.size()) {
                return;
            }

            ContactReport& out =
                report_.contacts[reportedContacts++];

            out.flags = flags;
            out.id = contact.id;
            out.x = contact.x;
            out.y = contact.y;
        };

    if (!needsBridge) {
        // Normal single-packet transition: all desired active contacts first,
        // then explicit UP entries for contacts that disappeared.
        for (
            std::uint8_t i = 0;
            i < desiredFrame_.count;
            ++i
        ) {
            appendReportContact(
                0x03u, // Tip Switch + In Range.
                desiredFrame_.contacts[i]
            );
        }

        for (
            std::uint8_t i = 0;
            i < committedFrame_.count;
            ++i
        ) {
            const oag::MobileTouchContact& previous =
                committedFrame_.contacts[i];

            if (
                findContactById(
                    desiredFrame_,
                    previous.id
                ) != nullptr
            ) {
                continue;
            }

            appendReportContact(
                0x00u, // Explicit UP for the same Contact ID.
                previous
            );
        }

        pendingTargetFrame_ = desiredFrame_;
    } else {
        // At most ten finger collections exist in one HID report. If a frame
        // change would need >10 active+release entries, never drop an UP.
        //
        // Bridge packet:
        //   1) keep only contacts that exist in BOTH old and new states,
        //   2) explicitly release every old contact that disappeared,
        //   3) delay brand-new contacts until the following packet.
        //
        // common + released == committedFrame_.count <= 10, so every release
        // is guaranteed to fit and Android can never be left with a lost UP.
        for (
            std::uint8_t i = 0;
            i < desiredFrame_.count;
            ++i
        ) {
            const oag::MobileTouchContact& current =
                desiredFrame_.contacts[i];

            if (
                findContactById(
                    committedFrame_,
                    current.id
                ) == nullptr
            ) {
                continue;
            }

            appendReportContact(0x03u, current);
            appendFrameContact(
                pendingTargetFrame_,
                current
            );
        }

        for (
            std::uint8_t i = 0;
            i < committedFrame_.count;
            ++i
        ) {
            const oag::MobileTouchContact& previous =
                committedFrame_.contacts[i];

            if (
                findContactById(
                    desiredFrame_,
                    previous.id
                ) != nullptr
            ) {
                continue;
            }

            appendReportContact(0x00u, previous);
        }
    }

    report_.contactCount = reportedContacts;
    pending_ = true;
}

bool MobileTouchOutput::pump() {
    // First preserve any already-built transition. It must never be
    // overwritten by a newer input state while endpoint 0x81 is busy.
    if (pending_) {
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

        committedFrame_ = pendingTargetFrame_;
        pending_ = false;
    }

    if (framesEqual(committedFrame_, desiredFrame_)) {
        return true;
    }

    preparePendingTransition();

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

    committedFrame_ = pendingTargetFrame_;
    pending_ = false;

    // If a bridge packet was required, desiredFrame_ still differs from the
    // newly committed intermediary frame. The next poll will enqueue the new
    // contacts after all releases are safely committed.
    return true;
}

} // namespace oag::firmware
