#pragma once

#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class BluetoothHostV2;

// Clean BLE HID output backend derived from JoypadOS' standard BLE gamepad
// architecture (Apache-2.0 upstream), adapted to OAG's LogicalGamepadState.
//
// Key contract:
// - this backend owns the single ATT server + HIDS Device profile;
// - BluetoothHostV2 attaches only client/input profiles afterwards;
// - Android connection is considered ready on the first HIDS input-report
//   subscription, matching JoypadOS exactly;
// - no peripheral-initiated pairing request is sent.
class JoypadBleOutput {
public:
    bool initialize(BluetoothHostV2& host);
    void poll();
    void submit(const oag::LogicalGamepadState& state);

    bool connected() const {
        return gamepadSubscribed_;
    }

    void handlePacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

private:
    struct __attribute__((packed)) GamepadReport {
        std::uint8_t buttonsLo = 0;
        std::uint8_t buttonsHi = 0;
        std::uint8_t hat = 0;
        std::int16_t lx = 16384;
        std::int16_t ly = 16384;
        std::int16_t rx = 16384;
        std::int16_t ry = 16384;
        std::int16_t lt = 0;
        std::int16_t rt = 0;
    };

    static constexpr std::uint16_t kInvalidHandle = 0xFFFFu;
    static constexpr std::uint8_t kGamepadReportId = 3u;

    static GamepadReport encode(
        const oag::LogicalGamepadState& state
    );

    void setAdvertising(bool enabled);
    void requestCanSend();
    void sendPending();
    void startSelfTest();
    void serviceSelfTest();

    BluetoothHostV2* host_ = nullptr;

    bool initialized_ = false;
    bool advertising_ = false;
    bool gamepadSubscribed_ = false;
    bool canSendPending_ = false;

    std::uint16_t rawLinkHandle_ = kInvalidHandle;
    std::uint16_t connectionHandle_ = kInvalidHandle;

    GamepadReport liveReport_ {};
    GamepadReport pendingReport_ {};
    GamepadReport lastSentReport_ {};
    bool pendingDirty_ = true;

    bool selfTestActive_ = false;
    std::uint32_t selfTestStartedMs_ = 0;
    std::uint8_t selfTestStep_ = 0;
};

} // namespace oag::firmware
