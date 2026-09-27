#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class BlePhoneGamepadOutput {
public:
    bool prepareAttDatabase();
    const std::uint8_t* attDatabase() const;
    bool installDeviceServices();

    void poll();
    void submit(const oag::LogicalGamepadState& state);

    bool adoptPeripheralConnection(std::uint16_t connectionHandle);
    bool ownsConnection(std::uint16_t connectionHandle) const;
    void handleDisconnection(std::uint16_t connectionHandle);

    void handleHidsPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    bool takeSubscriptionReadySignal();

    bool connected() const;
    bool subscribed() const;

private:
    static constexpr std::uint16_t kInvalidHandle = 0xFFFFu;
    static constexpr std::uint8_t kInputReportId = 1u;

    void startAdvertising();
    void requestCanSend();
    void sendCurrentReport();
    void startConnectionSelfTest();
    void serviceConnectionSelfTest();

    std::array<std::uint8_t, 1024> attDatabase_ {};
    std::size_t attDatabaseLength_ = 0;

    bool prepared_ = false;
    bool servicesInstalled_ = false;
    bool inputSubscribed_ = false;
    bool canSendPending_ = false;
    bool reportDirty_ = true;
    bool subscriptionReadySignal_ = false;

    bool selfTestActive_ = false;
    std::uint32_t selfTestStartedMs_ = 0;
    std::uint8_t selfTestStep_ = 0;

    std::uint16_t connectionHandle_ = kInvalidHandle;
    std::uint8_t protocolMode_ = 1;

    std::array<std::uint8_t, 17> report_ {};
    std::array<std::uint8_t, 17> liveReport_ {};
};

} // namespace oag::firmware
