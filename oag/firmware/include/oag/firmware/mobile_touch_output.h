#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"
#include "oag/output/touch/mobile_touch_mapper.h"

namespace oag::firmware {

class MobileTouchOutput {
public:
    static constexpr std::size_t kMaxContacts =
        oag::MobileTouchFrame::kMaxContacts;

    void task();

    bool send(
        std::uint8_t logicalSlot,
        const LogicalGamepadState& state
    );

    bool sendNeutral();

private:
    // USB polls at 1 ms. A 4 ms authoritative snapshot cadence is fast enough
    // for controls while leaving margin for the rest of the firmware.
    static constexpr std::uint64_t kSnapshotPeriodUs = 4000;

    // A physical Contact ID is not allowed to become DOWN again until this
    // many successfully accepted full snapshots have explicitly carried it as
    // Tip=0/InRange=0. This is acceptance-count based, not time based, so USB
    // backpressure can never silently consume the release barrier.
    static constexpr std::uint8_t kReleaseSnapshots = 8;

    // IDs 0,1,2 are permanently reserved for camera, movement and fire.
    // Remaining mapper actions are leased onto IDs 3..9.
    static constexpr std::uint8_t kPinnedContactCount = 3;

    struct __attribute__((packed)) ContactReport {
        std::uint8_t flags = 0;
        std::uint8_t id = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
    };

    struct __attribute__((packed)) Report {
        std::array<ContactReport, kMaxContacts> contacts {};
        std::uint8_t contactCount = 0;
    };

    struct PhysicalSlot {
        // Mapper/logical Contact ID currently owned by this physical ID.
        // Pinned physical IDs 0..2 always own the same logical ID.
        std::int16_t logicalId = -1;
        bool active = false;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint8_t releaseSnapshotsRemaining = 0;
    };

    static_assert(sizeof(ContactReport) == 6);
    static_assert(sizeof(Report) == 61);
    static_assert(kMaxContacts == 10);

    static const oag::MobileTouchContact* findDesiredByLogicalId(
        const oag::MobileTouchFrame& frame,
        std::uint8_t logicalId
    );

    bool logicalIdOwned(std::uint8_t logicalId) const;
    void reconcileDesiredState();
    void buildAuthoritativeReport();
    void commitAcceptedSnapshot();
    bool pump(bool forceImmediate);

    oag::MobileTouchMapper mapper_ {};
    oag::MobileTouchFrame desiredFrame_ {};

    // report slot index == USB Contact Identifier, permanently. This prevents
    // a logical finger from jumping between HID Finger collections when other
    // fingers appear/disappear.
    std::array<PhysicalSlot, kMaxContacts> slots_ {};

    Report report_ {};
    std::uint64_t nextSnapshotUs_ = 0;
    bool initialized_ = false;
};

} // namespace oag::firmware
