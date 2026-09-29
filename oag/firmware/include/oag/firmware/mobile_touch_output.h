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
    // 250 Hz keeps active contacts refreshed well above common display/input
    // refresh rates while leaving headroom for the rest of the firmware.
    static constexpr std::uint64_t kSnapshotPeriodUs = 4000;

    // U2HTS-style release semantics: a released Contact ID remains present
    // briefly with Tip=0/InRange=0, then disappears from subsequent reports.
    // Multiple accepted reports add resilience without creating a long
    // re-press delay.
    static constexpr std::uint8_t kReleaseSnapshots = 3;

    // Linux hid-multitouch has a 100 ms "sticky fingers" recovery concept.
    // Here the same threshold is used only for a device-side USB transport
    // stall: if active contacts cannot be delivered for this long, force an
    // all-UP recovery before allowing touches to become active again.
    static constexpr std::uint64_t kTransportStallUs = 100000;
    static constexpr std::uint8_t kRecoveryReleaseSnapshots = 4;

    // IDs 0,1,2 are permanently reserved for camera, movement and fire.
    // Remaining mapper actions are leased onto stable USB Contact IDs 3..9.
    static constexpr std::uint8_t kPinnedContactCount = 3;

    struct __attribute__((packed)) ContactReport {
        std::uint8_t flags = 0;
        std::uint8_t id = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
    };

    struct __attribute__((packed)) Report {
        std::array<ContactReport, kMaxContacts> contacts {};
        std::uint16_t scanTime = 0;
        std::uint8_t contactCount = 0;
    };

    struct PhysicalSlot {
        std::int16_t logicalId = -1;
        bool active = false;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint8_t releaseSnapshotsRemaining = 0;
    };

    static_assert(sizeof(ContactReport) == 6);
    static_assert(sizeof(Report) == 63);
    static_assert(kMaxContacts == 10);

    static const oag::MobileTouchContact* findDesiredByLogicalId(
        const oag::MobileTouchFrame& frame,
        std::uint8_t logicalId
    );

    bool logicalIdOwned(std::uint8_t logicalId) const;
    bool hasActiveContacts() const;
    bool hasPendingRelease() const;

    void initializeSlots();
    void reconcileDesiredState();
    void enterTransportRecovery();
    void buildReport(std::uint64_t nowUs);
    void commitAcceptedSnapshot(std::uint64_t nowUs);
    bool pump(bool forceImmediate);

    oag::MobileTouchMapper mapper_ {};
    oag::MobileTouchFrame desiredFrame_ {};

    std::array<PhysicalSlot, kMaxContacts> slots_ {};

    Report report_ {};
    std::uint64_t nextSnapshotUs_ = 0;
    std::uint64_t lastAcceptedReportUs_ = 0;
    bool initialized_ = false;
    bool transportRecoveryActive_ = false;
};

} // namespace oag::firmware
