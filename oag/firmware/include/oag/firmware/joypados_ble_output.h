#pragma once

#include <cstdint>

#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class BluetoothHostV2;

// Arduino-Pico JoystickBLE-compatible BLE HID backend.
//
// The HID/GATT/runtime contract intentionally mirrors the standalone
// Arduino-Pico JoystickBLE path that hardware-tested successfully on Android:
// - joystick-only HID descriptor uses Report ID 1;
// - payload is packed hid_gamepad16_report_t-compatible 17 bytes;
// - HIDS session opens on regular input-report enable OR boot-keyboard enable;
// - CAN_SEND_NOW transmits via hids_device_send_input_report_for_id().
// JoypadOS contributes only the Host+Peripheral coexistence ownership model.
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
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::int16_t z = 0;
        std::int16_t rz = 0;
        std::int16_t rx = -32767;
        std::int16_t ry = -32767;
        std::uint8_t hat = 0;
        std::uint32_t buttons = 0;
    };

    static_assert(sizeof(GamepadReport) == 17);

    static constexpr std::uint16_t kInvalidHandle = 0xFFFFu;
    static constexpr std::uint8_t kGamepadReportId = 1u;

    static GamepadReport encode(
        const oag::LogicalGamepadState& state
    );

    void setAdvertising(bool enabled);
    void requestCanSend();
    void sendPending();
    void startSelfTest();
    void serviceSelfTest();
    void openHidsSession(std::uint16_t handle);

    BluetoothHostV2* host_ = nullptr;

    bool initialized_ = false;
    bool advertising_ = false;
    bool gamepadSubscribed_ = false;
    bool canSendPending_ = false;

    std::uint16_t rawLinkHandle_ = kInvalidHandle;
    std::uint16_t connectionHandle_ = kInvalidHandle;
    std::uint8_t protocolMode_ = 1u;

    GamepadReport liveReport_ {};
    GamepadReport pendingReport_ {};
    GamepadReport lastSentReport_ {};
    bool pendingDirty_ = true;

    bool selfTestActive_ = false;
    std::uint32_t selfTestStartedMs_ = 0;
    std::uint8_t selfTestStep_ = 0;
};

} // namespace oag::firmware
