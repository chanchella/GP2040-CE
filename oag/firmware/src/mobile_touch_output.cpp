#include "oag/firmware/mobile_touch_output.h"

#include <cstddef>
#include <cstdint>

#include "pico/time.h"
#include "tusb.h"

namespace oag::firmware {
namespace {

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

const oag::MobileTouchContact*
MobileTouchOutput::findContactById(
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

bool MobileTouchOutput::releaseFenceActive(
    std::uint8_t id
) const {
    return
        id < releaseFences_.size() &&
        releaseFences_[id].active;
}

bool MobileTouchOutput::anyReleaseFenceActive() const {
    for (const ReleaseFence& fence : releaseFences_) {
        if (fence.active) {
            return true;
        }
    }

    return false;
}

std::uint8_t MobileTouchOutput::releaseFenceCount() const {
    std::uint8_t count = 0;

    for (const ReleaseFence& fence : releaseFences_) {
        if (fence.active) {
            ++count;
        }
    }

    return count;
}

void MobileTouchOutput::startReleaseFence(
    const oag::MobileTouchContact& contact
) {
    if (contact.id >= releaseFences_.size()) {
        return;
    }

    ReleaseFence& fence = releaseFences_[contact.id];

    if (fence.active) {
        return;
    }

    fence.active = true;
    fence.contact = contact;
    fence.remainingReports = kReleaseRepeatReports;

    // If this release eventually leaves the entire surface empty, follow it
    // with explicit all-zero sync reports.
    zeroSyncArmed_ = true;
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
    // and movement gesture machines reset with the USB touch lifecycle.
    desiredFrame_ = mapper_.map(LogicalGamepadState {});
    pump();
    return true;
}

void MobileTouchOutput::prepareZeroSyncReport() {
    report_ = {};
    pendingTargetFrame_ = {};
    pendingReleaseMask_ = 0;
    pendingIsZeroSync_ = true;
    pending_ = true;
}

void MobileTouchOutput::preparePendingTransition() {
    report_ = {};
    pendingTargetFrame_ = {};
    pendingReleaseMask_ = 0;
    pendingIsZeroSync_ = false;

    // First detect every active contact that disappeared from the desired
    // frame and convert it into a durable release fence.
    for (std::uint8_t i = 0; i < committedFrame_.count; ++i) {
        const oag::MobileTouchContact& previous =
            committedFrame_.contacts[i];

        if (
            findContactById(
                desiredFrame_,
                previous.id
            ) == nullptr
        ) {
            startReleaseFence(previous);
        }
    }

    const std::uint8_t fenceCount =
        releaseFenceCount();

    std::uint8_t reportedContacts = 0;

    const auto appendReportContact =
        [this, &reportedContacts](
            std::uint8_t flags,
            const oag::MobileTouchContact& contact
        ) {
            if (reportedContacts >= report_.contacts.size()) {
                return false;
            }

            ContactReport& out =
                report_.contacts[reportedContacts++];

            out.flags = flags;
            out.id = contact.id;
            out.x = contact.x;
            out.y = contact.y;
            return true;
        };

    // Release fences have absolute priority. A newly requested contact can be
    // delayed for a few milliseconds, but an UP must never be dropped.
    //
    // Continuing contacts from committedFrame_ are also protected: fences
    // plus continuing contacts can never exceed the previous <=10-contact
    // frame. New contacts are admitted only into the remaining capacity.
    for (std::uint8_t i = 0; i < desiredFrame_.count; ++i) {
        const oag::MobileTouchContact& current =
            desiredFrame_.contacts[i];

        if (releaseFenceActive(current.id)) {
            continue;
        }

        if (
            findContactById(
                committedFrame_,
                current.id
            ) == nullptr
        ) {
            continue;
        }

        if (appendReportContact(0x03u, current)) {
            appendFrameContact(
                pendingTargetFrame_,
                current
            );
        }
    }

    for (std::uint8_t id = 0; id < releaseFences_.size(); ++id) {
        const ReleaseFence& fence =
            releaseFences_[id];

        if (!fence.active) {
            continue;
        }

        if (
            appendReportContact(
                0x00u,
                fence.contact
            )
        ) {
            pendingReleaseMask_ |=
                static_cast<std::uint16_t>(
                    1u << id
                );
        }
    }

    // Remaining slots can take brand-new contacts whose IDs are not in a
    // release fence. This is what prevents immediate Contact ID reuse from
    // resurrecting a stale Android finger.
    for (std::uint8_t i = 0; i < desiredFrame_.count; ++i) {
        const oag::MobileTouchContact& current =
            desiredFrame_.contacts[i];

        if (releaseFenceActive(current.id)) {
            continue;
        }

        if (
            findContactById(
                committedFrame_,
                current.id
            ) != nullptr
        ) {
            continue;
        }

        if (reportedContacts >= kMaxContacts) {
            break;
        }

        if (appendReportContact(0x03u, current)) {
            appendFrameContact(
                pendingTargetFrame_,
                current
            );
        }
    }

    // Defensive invariant: all active release fences should fit because the
    // number of fences can never exceed the last committed <=10 contacts.
    // If this is ever violated, preserve already-added UP entries and delay
    // everything else rather than fabricate state.
    (void)fenceCount;

    report_.contactCount = reportedContacts;
    pending_ = true;
}

void MobileTouchOutput::commitAcceptedReport() {
    committedFrame_ = pendingTargetFrame_;

    if (pendingIsZeroSync_) {
        if (zeroSyncRemaining_ > 0) {
            --zeroSyncRemaining_;
        }

        pendingIsZeroSync_ = false;
    }

    const std::uint16_t releaseMask =
        pendingReleaseMask_;

    pendingReleaseMask_ = 0;

    for (std::uint8_t id = 0; id < releaseFences_.size(); ++id) {
        if (
            (releaseMask & static_cast<std::uint16_t>(1u << id)) == 0
        ) {
            continue;
        }

        ReleaseFence& fence =
            releaseFences_[id];

        if (!fence.active) {
            continue;
        }

        if (fence.remainingReports > 0) {
            --fence.remainingReports;
        }

        if (fence.remainingReports == 0) {
            fence = {};
        }
    }

    // Only when the entire logical surface is truly empty do we emit the
    // post-release zero synchronization fence. Do not interrupt other active
    // fingers merely because one button was released.
    if (
        zeroSyncArmed_ &&
        !anyReleaseFenceActive() &&
        committedFrame_.count == 0 &&
        desiredFrame_.count == 0 &&
        zeroSyncRemaining_ == 0
    ) {
        zeroSyncRemaining_ = kZeroSyncReports;
        zeroSyncArmed_ = false;
    }

    lastAcceptedReportUs_ = time_us_64();
}

bool MobileTouchOutput::pump() {
    // Never overwrite an unsent report. A newer logical state can update
    // desiredFrame_, but the exact pending DOWN/UP packet must reach TinyUSB
    // before another transition is constructed.
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

        pending_ = false;
        commitAcceptedReport();
    }

    // Repeated release fences outrank everything else. Each accepted packet
    // repeats Tip=0 for the same Contact ID and last X/Y, while unrelated
    // active contacts continue normally.
    if (anyReleaseFenceActive()) {
        preparePendingTransition();
    } else if (zeroSyncRemaining_ > 0) {
        // All fingers are already released. Send explicit empty reports before
        // any future ID reuse only while the desired surface remains empty.
        if (
            desiredFrame_.count == 0 &&
            committedFrame_.count == 0
        ) {
            prepareZeroSyncReport();
        } else {
            zeroSyncRemaining_ = 0;
        }
    } else if (!framesEqual(committedFrame_, desiredFrame_)) {
        preparePendingTransition();
    } else if (desiredFrame_.count > 0) {
        const std::uint64_t nowUs = time_us_64();

        // Keep active contacts alive at 125 Hz.
        if (
            lastAcceptedReportUs_ != 0 &&
            nowUs - lastAcceptedReportUs_ < kActiveHeartbeatUs
        ) {
            return true;
        }

        preparePendingTransition();
    } else {
        return true;
    }

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
    commitAcceptedReport();
    return true;
}

} // namespace oag::firmware
