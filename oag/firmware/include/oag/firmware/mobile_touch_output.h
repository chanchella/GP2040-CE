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

    static_assert(sizeof(ContactReport) == 6);
    static_assert(sizeof(Report) == 61);

    static bool framesEqual(
        const oag::MobileTouchFrame& a,
        const oag::MobileTouchFrame& b
    );

    bool pump();
    void preparePendingTransition();

    oag::MobileTouchMapper mapper_ {};

    // committedFrame_ is the last frame actually accepted by TinyUSB.
    // desiredFrame_ is the newest logical touch state.
    //
    // This separation is critical: input can change faster than endpoint 0x81
    // becomes ready. Never overwrite an unsent release report or advance the
    // committed contact lifecycle before USB has accepted that report.
    oag::MobileTouchFrame committedFrame_ {};
    oag::MobileTouchFrame desiredFrame_ {};
    oag::MobileTouchFrame pendingTargetFrame_ {};

    Report report_ {};
    bool pending_ = false;
    std::uint64_t lastAcceptedReportUs_ = 0;
};

} // namespace oag::firmware
