#include "oag/firmware/mobile_touch_output.h"

#include <algorithm>
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

bool MobileTouchOutput::hasActiveContacts() const {
    for (const PhysicalSlot& slot : slots_) {
        if (slot.active) {
            return true;
        }
    }

    return false;
}

bool MobileTouchOutput::hasPendingRelease() const {
    for (const PhysicalSlot& slot : slots_) {
        if (slot.releaseSnapshotsRemaining != 0) {
            return true;
        }
    }

    return false;
}

void MobileTouchOutput::initializeSlots() {
    if (initialized_) {
        return;
    }

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

void MobileTouchOutput::reconcileDesiredState() {
    initializeSlots();

    // Pinned camera/movement/fire IDs never exchange identities.
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
                slot.releaseSnapshotsRemaining =
                    std::max(
                        slot.releaseSnapshotsRemaining,
                        kReleaseSnapshots
                    );
            }
            continue;
        }

        slot.x = desired->x;
        slot.y = desired->y;

        // Never reactivate a Contact ID until its accepted UP records have
        // completed. During a transport recovery, every contact must remain
        // UP until the recovery barrier is fully delivered.
        if (
            !transportRecoveryActive_ &&
            !slot.active &&
            slot.releaseSnapshotsRemaining == 0
        ) {
            slot.active = true;
        }
    }

    // Update or release dynamically leased IDs.
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
                slot.releaseSnapshotsRemaining =
                    std::max(
                        slot.releaseSnapshotsRemaining,
                        kReleaseSnapshots
                    );
            } else if (
                slot.releaseSnapshotsRemaining == 0 &&
                !transportRecoveryActive_
            ) {
                slot.logicalId = -1;
            }
            continue;
        }

        slot.x = desired->x;
        slot.y = desired->y;

        if (
            !transportRecoveryActive_ &&
            !slot.active &&
            slot.releaseSnapshotsRemaining == 0
        ) {
            slot.active = true;
        }
    }

    if (transportRecoveryActive_) {
        return;
    }

    // Lease logical actions 3..15 onto stable USB Contact IDs 3..9.
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

void MobileTouchOutput::enterTransportRecovery() {
    initializeSlots();

    transportRecoveryActive_ = true;

    // Convert every owned/active contact into an explicit UP record. Keep its
    // last coordinates and Contact ID, exactly like real multitouch devices
    // that append released contacts to the next report.
    for (std::size_t physicalId = 0;
         physicalId < slots_.size();
         ++physicalId) {
        PhysicalSlot& slot = slots_[physicalId];

        if (
            slot.active ||
            slot.releaseSnapshotsRemaining != 0 ||
            slot.logicalId >= 0
        ) {
            slot.active = false;
            slot.releaseSnapshotsRemaining =
                std::max(
                    slot.releaseSnapshotsRemaining,
                    kRecoveryReleaseSnapshots
                );
        }
    }
}

void MobileTouchOutput::buildReport(std::uint64_t nowUs) {
    report_ = {};

    std::size_t recordIndex = 0;

    // U2HTS-style hybrid report:
    //   1) active contacts first,
    //   2) explicit released contacts (Tip=0/InRange=0),
    //   3) remaining fixed HID collections left zero and ignored because
    //      Contact Count only covers records 0..contactCount-1.
    for (std::size_t physicalId = 0;
         physicalId < slots_.size() && recordIndex < kMaxContacts;
         ++physicalId) {
        const PhysicalSlot& slot = slots_[physicalId];

        if (!slot.active) {
            continue;
        }

        ContactReport& out = report_.contacts[recordIndex++];
        out.flags = 0x03u;
        out.id = static_cast<std::uint8_t>(physicalId);
        out.x = slot.x;
        out.y = slot.y;
    }

    for (std::size_t physicalId = 0;
         physicalId < slots_.size() && recordIndex < kMaxContacts;
         ++physicalId) {
        const PhysicalSlot& slot = slots_[physicalId];

        if (
            slot.active ||
            slot.releaseSnapshotsRemaining == 0
        ) {
            continue;
        }

        ContactReport& out = report_.contacts[recordIndex++];
        out.flags = 0x00u;
        out.id = static_cast<std::uint8_t>(physicalId);
        out.x = slot.x;
        out.y = slot.y;
    }

    // HID Scan Time units are 100 microseconds (10^-4 seconds).
    report_.scanTime =
        static_cast<std::uint16_t>(
            (nowUs / 100u) & 0xFFFFu
        );

    // This is intentionally the number of records that carry actual active or
    // release information, not the descriptor's maximum contact capacity.
    report_.contactCount =
        static_cast<std::uint8_t>(recordIndex);
}

void MobileTouchOutput::commitAcceptedSnapshot(
    std::uint64_t nowUs
) {
    for (PhysicalSlot& slot : slots_) {
        if (
            !slot.active &&
            slot.releaseSnapshotsRemaining > 0
        ) {
            --slot.releaseSnapshotsRemaining;
        }
    }

    lastAcceptedReportUs_ = nowUs;

    if (
        transportRecoveryActive_ &&
        !hasPendingRelease()
    ) {
        transportRecoveryActive_ = false;

        // Dynamic leases may now be returned or reactivated from the current
        // desired frame. Pinned 0/1/2 remain permanently reserved.
        for (std::size_t physicalId = kPinnedContactCount;
             physicalId < slots_.size();
             ++physicalId) {
            PhysicalSlot& slot = slots_[physicalId];

            if (!slot.active && slot.releaseSnapshotsRemaining == 0) {
                const bool stillDesired =
                    slot.logicalId >= 0 &&
                    findDesiredByLogicalId(
                        desiredFrame_,
                        static_cast<std::uint8_t>(slot.logicalId)
                    ) != nullptr;

                if (!stillDesired) {
                    slot.logicalId = -1;
                }
            }
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
    desiredFrame_ = mapper_.map(LogicalGamepadState {});
    reconcileDesiredState();
    pump(true);
    return true;
}

bool MobileTouchOutput::pump(bool forceImmediate) {
    const std::uint64_t nowUs = time_us_64();

    // Device-side equivalent of the Linux sticky-finger safety net. This only
    // fires if USB transport delivery itself has stalled while contacts are
    // active; it does NOT impose a maximum duration on a legitimate hold.
    if (
        !transportRecoveryActive_ &&
        hasActiveContacts() &&
        lastAcceptedReportUs_ != 0 &&
        nowUs - lastAcceptedReportUs_ >= kTransportStallUs
    ) {
        enterTransportRecovery();
    }

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

    buildReport(nowUs);

    if (!tud_hid_n_report(
            0,
            0,
            &report_,
            sizeof(report_)
        )) {
        return false;
    }

    commitAcceptedSnapshot(nowUs);
    nextSnapshotUs_ = nowUs + kSnapshotPeriodUs;

    // Accepted UP records may have completed a release/recovery barrier.
    reconcileDesiredState();
    return true;
}

} // namespace oag::firmware
