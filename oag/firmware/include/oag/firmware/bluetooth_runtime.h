#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/device/device_registry.h"
#include "oag/output/logical_gamepad_state.h"

namespace oag::firmware {

class BluetoothRuntimeObserver {
public:
    virtual ~BluetoothRuntimeObserver() = default;

    virtual void onBluetoothHidDescriptor(
        TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        const std::uint8_t* descriptor,
        std::size_t descriptorLength
    ) = 0;

    virtual void onBluetoothHidReport(
        TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        const std::uint8_t* report,
        std::size_t reportLength
    ) = 0;

    virtual void onBluetoothHidDisconnected(
        TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance
    ) = 0;
};

class BluetoothRuntime {
public:
    static constexpr std::size_t kConnectionBudget = 6;
    static constexpr std::size_t kMaxHidServicesPerLeDevice = 3;

    bool initialize(BluetoothRuntimeObserver& observer);
    void poll();

    bool beginDiscovery();

    void submitPeripheralGamepad(
        const LogicalGamepadState& state
    );

    bool peripheralConnected() const {
        return false;
    }

    bool initialized() const {
        return initialized_;
    }

    // Called by the C BTstack thunks in the translation unit.
    void handleHciPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void handleClassicHidPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void handleLeHidPacket(
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

    void handleDiscoveryTimer();

private:
    struct ClassicLink {
        bool active = false;
        std::uint16_t hidCid = 0;
        std::uint16_t connectionHandle = 0;
        std::array<std::uint8_t, 6> address {};
    };

    struct LeLink {
        bool active = false;
        std::uint16_t connectionHandle = 0;
        std::uint16_t hidsCid = 0;
        std::uint8_t serviceCount = 0;
        std::uint8_t addressType = 0;
        std::array<std::uint8_t, 6> address {};
    };

    enum class DiscoveryPhase : std::uint8_t {
        Idle = 0,
        ClassicInquiry,
        LeScan,
        PausedForConnection,
    };

    bool hasFreeConnectionBudget() const;
    std::size_t activeConnectionCount() const;

    ClassicLink* findClassicByCid(std::uint16_t hidCid);
    ClassicLink* findClassicByHandle(std::uint16_t handle);
    ClassicLink* allocateClassic();

    LeLink* findLeByHandle(std::uint16_t handle);
    LeLink* findLeByCid(std::uint16_t hidsCid);
    LeLink* allocateLe();

    bool addressAlreadyConnected(
        const std::uint8_t* address
    ) const;

    void startClassicInquiry();
    void startLeScan();
    void resumeDiscovery();
    void stopDiscoveryTimer();
    void scheduleDiscoveryTimer(std::uint32_t timeoutMs);
    void serviceDiagnosticLed();
    void migrateBondStateOnce();

    void startLeHids(std::uint16_t connectionHandle);
    void notifyLeDescriptors(LeLink& link);
    void disconnectLeServices(LeLink& link);

    // Bluetooth Output/Peripheral intentionally dormant in U8E.
    // It returns after the Host pairing gate is hardware-verified.

    BluetoothRuntimeObserver* observer_ = nullptr;
    bool initialized_ = false;
    bool hciWorking_ = false;

    DiscoveryPhase discoveryPhase_ = DiscoveryPhase::Idle;

    bool classicConnectPending_ = false;
    bool leConnectPending_ = false;

    std::uint64_t diagnosticLastToggleUs_ = 0;
    bool diagnosticLedState_ = false;

    std::array<ClassicLink, kConnectionBudget> classicLinks_ {};
    std::array<LeLink, kConnectionBudget> leLinks_ {};

    std::array<std::uint8_t, 4096> classicDescriptorStorage_ {};
    std::array<std::uint8_t, 4096> leDescriptorStorage_ {};
    std::array<std::uint8_t, 520> normalizedReport_ {};

    LogicalGamepadState peripheralGamepadState_ {};
};

} // namespace oag::firmware
