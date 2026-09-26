#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class BluetoothHostV2;

// Generic BLE HID Gamepad output built as a separate peripheral layer over the
// UI5K Bluetooth runtime BTstack runtime. It deliberately does not own or
// reinitialize CYW43, L2CAP, SM, or the Bluetooth input-host stack.
class BluetoothPlatformOutput {
public:
    bool initialize(BluetoothHostV2& host);
    void poll();
    void submit(const oag::LogicalGamepadState& state);

    bool connected() const {
        return connectionHandle_ != kInvalidHandle;
    }

    void handleHciPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void handleSmPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void handleHidsPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void copyCurrentInputReport(
        std::uint8_t* out,
        std::uint16_t maxSize
    ) const;

private:
    static constexpr std::uint16_t kInvalidHandle = 0xFFFFu;
    static constexpr std::uint8_t kInputReportId = 1u;

    void startAdvertising();
    void requestCanSend();
    void sendCurrentReport();
    void startConnectionSelfTest();
    void serviceConnectionSelfTest();

    BluetoothHostV2* host_ = nullptr;

    bool initialized_ = false;
    bool hciWorking_ = false;
    bool inputSubscribed_ = false;
    bool canSendPending_ = false;
    bool reportDirty_ = true;

    bool selfTestActive_ = false;
    std::uint32_t selfTestStartedMs_ = 0;
    std::uint8_t selfTestStep_ = 0;

    std::uint16_t connectionHandle_ = kInvalidHandle;
    std::uint8_t peerAddressType_ = 0;
    std::array<std::uint8_t, 6> peerAddress_ {};

    // report_ is the currently transmitted BLE report.
    // liveReport_ always tracks the real Primary controller state so OUT9 can
    // run a short synthetic connection proof, then hand over seamlessly.
    std::array<std::uint8_t, 17> report_ {};
    std::array<std::uint8_t, 17> liveReport_ {};
};

} // namespace oag::firmware
