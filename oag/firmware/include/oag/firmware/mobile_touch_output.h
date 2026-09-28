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
    static constexpr std::uint64_t kActiveHeartbeatUs = 8000;
    static constexpr std::uint8_t kContactIdCount = 16;
    static constexpr std::uint8_t kReleaseRepeatReports = 4;
    static constexpr std::uint8_t kZeroSyncReports = 2;

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

    struct ReleaseFence {
        bool active = false;
        oag::MobileTouchContact contact {};
        std::uint8_t remainingReports = 0;
    };

    static_assert(sizeof(ContactReport) == 6);
    static_assert(sizeof(Report) == 61);

    static bool framesEqual(
        const oag::MobileTouchFrame& a,
        const oag::MobileTouchFrame& b
    );

    static const oag::MobileTouchContact* findContactById(
        const oag::MobileTouchFrame& frame,
        std::uint8_t id
    );

    bool releaseFenceActive(std::uint8_t id) const;
    bool anyReleaseFenceActive() const;
    std::uint8_t releaseFenceCount() const;

    void startReleaseFence(
        const oag::MobileTouchContact& contact
    );

    void preparePendingTransition();
    void prepareZeroSyncReport();
    void commitAcceptedReport();
    bool pump();

    oag::MobileTouchMapper mapper_ {};

    // committedFrame_ is the ACTIVE touch state last accepted by TinyUSB.
    // desiredFrame_ is the newest logical touch state requested by the mapper.
    //
    // A disappearing contact is NOT removed from the host lifecycle with one
    // best-effort UP packet. It enters a short release fence:
    //   same Contact ID + last X/Y + Tip Switch clear
    // repeated across several accepted reports.
    //
    // While a Contact ID is fenced, that ID cannot be reused for a new DOWN.
    // This prevents Android from merging a fresh press into a stale finger.
    oag::MobileTouchFrame committedFrame_ {};
    oag::MobileTouchFrame desiredFrame_ {};
    oag::MobileTouchFrame pendingTargetFrame_ {};

    std::array<ReleaseFence, kContactIdCount> releaseFences_ {};

    Report report_ {};
    std::uint16_t pendingReleaseMask_ = 0;
    bool pending_ = false;
    bool pendingIsZeroSync_ = false;

    // After the final finger on the surface has completed its repeated UP
    // fence, send two explicit all-zero frames. This gives Android a clean
    // "surface empty" synchronization point before future Contact ID reuse.
    bool zeroSyncArmed_ = false;
    std::uint8_t zeroSyncRemaining_ = 0;

    std::uint64_t lastAcceptedReportUs_ = 0;
};

} // namespace oag::firmware
