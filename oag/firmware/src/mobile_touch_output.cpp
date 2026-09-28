#include "oag/firmware/mobile_touch_output.h"

#include <cstddef>
#include <cstdint>

#include "pico/time.h"
#include "tusb.h"

namespace oag::firmware {

const oag::MobileTouchContact*
MobileTouchOutput::findDesiredByLogicalId(
    const oag::MobileTouchFrame& frame,
    std::uint8_t logicalId
) {
    for (std::uint8_t i = 0; i < frame.count; ++i) {
        if (frame.contacts[i].id == logicalId) {
            return &frame.contacts[i];
        }
    }

    return nullptr;
}

bool MobileTouchOutput::logicalIdOwned(
    std::uint8_t logicalId
) const {
    for (const PhysicalSlot& slot : slots_) {
        if (slot.logicalId == logicalId) {
            return true;
        }
    }

    return false;
}

void MobileTouchOutput::reconcileDesiredState() {
    if (!initialized_) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            PhysicalSlot& slot = slots_[i];
            slot = {};
            slot.logicalId =
                i < kPinnedContactCount
                    ? static_cast<std::int16_t>(i)
                    : static_cast<std::int16_t>(-1);
        }
        initialized_ = true;
    }

    // 1) Pinned camera/movement/fire IDs. They can never trade identities.
    for (std::uint8_t physicalId = 0;
         physicalId < kPinnedContactCount;
         ++physicalId) {
        PhysicalSlot& slot = slots_[physicalId];
        const oag::MobileTouchContact* desired =
            findDesiredByLogicalId(
                desiredFrame_,
                physicalId
            );

        if (desired == nullptr) {
            if (slot.active) {
                slot.active = false;
                slot.releaseSnapshotsRemaining = kReleaseSnapshots;
            }
            continue;
        }

        slot.x = desired->x;
        slot.y = desired->y;

        // A quick UP -> DOWN is held behind the accepted-snapshot barrier.
        // This guarantees the host actually observes the UP before the same
        // Contact ID is allowed to become active again.
        if (
            !slot.active &&
            slot.releaseSnapshotsRemaining == 0
        ) {
            slot.active = true;
        }
    }

    // 2) Release dynamic leases whose mapper action disappeared, and update
    // coordinates for continuing actions.
    for (std::size_t physicalId = kPinnedContactCount;
         physicalId < slots_.size();
         ++physicalId) {
        PhysicalSlot& slot = slots_[physicalId];

        if (slot.logicalId < 0) {
            continue;
        }

        const auto logicalId =
            static_cast<std::uint8_t>(slot.logicalId);
        const oag::MobileTouchContact* desired =
            findDesiredByLogicalId(
                desiredFrame_,
                logicalId
            );

        if (desired == nullptr) {
            if (slot.active) {
                slot.active = false;
                slot.releaseSnapshotsRemaining = kReleaseSnapshots;
            } else if (slot.releaseSnapshotsRemaining == 0) {
                // The release barrier has been observed by the host. The
                // physical ID may now be leased to a different action.
                slot.logicalId = -1;
            }
            continue;
        }

        slot.x = desired->x;
        slot.y = desired->y;

        if (
            !slot.active &&
            slot.releaseSnapshotsRemaining == 0
        ) {
            // Same logical action re-pressed after its release fence: reuse
            // the same physical ID instead of creating a second finger.
            slot.active = true;
        }
    }

    // 3) Lease mapper actions 3..15 onto stable physical IDs 3..9. Mapper
    // order is already the gameplay priority order, so allocation follows it.
    for (std::uint8_t i = 0; i < desiredFrame_.count; ++i) {
        const oag::MobileTouchContact& desired =
            desiredFrame_.contacts[i];

        if (desired.id < kPinnedContactCount) {
            continue;
        }

        if (logicalIdOwned(desired.id)) {
            continue;
        }

        for (std::size_t physicalId = kPinnedContactCount;
             physicalId < slots_.size();
             ++physicalId) {
            PhysicalSlot& slot = slots_[physicalId];

            if (
                slot.logicalId >= 0 ||
                slot.releaseSnapshotsRemaining != 0
            ) {
                continue;
            }

            slot.logicalId = desired.id;
            slot.active = true;
            slot.x = desired.x;
            slot.y = desired.y;
            break;
        }
    }
}

void MobileTouchOutput::buildAuthoritativeReport() {
    report_ = {};

    // Always transmit all 10 HID Finger collections, in a permanent mapping:
    // report collection index == Contact Identifier. Inactive contacts are
    // explicit Tip=0/InRange=0 records instead of being omitted.
    for (std::size_t physicalId = 0;
        physicalId < slots_.size();
        ++physicalId) {
        const PhysicalSlot& slot = slots_[physicalId];
        ContactReport& out = report_.contacts[physicalId];
        out.flags = slot.active ? 0x03u : 0x00u;
        out.id = static_cast<std::uint8_t>(physicalId);
        out.x = slot.x;
        out.y = slot.y;
    }

    // Contact Count is the number of contact RECORDS in this hybrid report.
    // Tip Switch/In Range determine which of those records are active.
    report_.contactCount =
        static_cast<std::uint8_t>(slots_.size());
}

void MobileTouchOutput::commitAcceptedSnapshot() {
    // Only accepted USB snapshots advance a release barrier. If TinyUSB is
    // backpressured, the barrier cannot expire behind the host's back.
    for (PhysicalSlot& slot : slots_) {
        if (
            !slot.active &&
            slot.releaseSnapshotsRemaining > 0
        ) {
            --slot.releaseSnapshotsRemaining;
        }
    }
}

void MobileTouchOutput::task() {
    reconcileDesiredState();
    pump(false);
}

bool MobileTouchOutput::send(
    std::uint8_t logicalSlot,
    const LogicalGamepadState& state
) {
    // Profile 7 exposes one touchscreen. Only Primary/output slot 0 owns it.
    if (logicalSlot != 0) {
        return true;
    }

    desiredFrame_ = mapper_.map(state);
    reconcileDesiredState();
    pump(true);
    return true;
}

bool MobileTouchOutput::sendNeutral() {
    // Disconnected state also resets the mapper's internal camera/movement
    // gesture state, then the authoritative snapshot engine explicitly clears
    // every physical Contact ID.
    desiredFrame_ = mapper_.map(LogicalGamepadState {});
    reconcileDesiredState();
    pump(true);
    return true;
}

bool MobileTouchOutput::pump(bool forceImmediate) {
    const std::uint64_t nowUs = time_us_64();

    if (
        !forceImmediate &&
        nextSnapshotUs_ != 0 &&
        nowUs < nextSnapshotUs_
    ) {
        return true;
    }

    if (!tud_hid_n_ready(0)) {
        return false;
    }

    buildAuthoritativeReport();

    if (!tud_hid_n_report(
            0,
            0,
            &report_,
            sizeof(report_)
        )) {
        return false;
    }

    commitAcceptedSnapshot();
    nextSnapshotUs_ = nowUs + kSnapshotPeriodUs;

    // A successfully accepted release snapshot may have completed a barrier.
    // Reconcile immediately so the next report can safely reactivate/release
    // leases without waiting for unrelated input.
    reconcileDesiredState();
    return true;
}

} // namespace oag::firmware
