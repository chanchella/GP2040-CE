#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oag/device/device_registry.h"

namespace oag::firmware {

enum class BluetoothHidOutputResult : std::uint8_t {
    Accepted = 0,
    Busy,
    Failed,
};

class BluetoothHostV2Observer {
public:
    virtual ~BluetoothHostV2Observer() = default;

    virtual void onBluetoothHidReady(
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
        const std::uint8_t* descriptor,
        std::size_t descriptorLength,
        const std::uint8_t* report,
        std::size_t reportLength
    ) = 0;

    virtual void onBluetoothHidDisconnected(
        TransportType transport,
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance
    ) = 0;
};

class BluetoothHostV2 {
public:
    static constexpr std::size_t kMaxPeers = 4;
    static constexpr std::size_t kMaxServicesPerPeer = 3;

    bool initialize(BluetoothHostV2Observer& observer);

    // OUT7: separate stack/service registration from HCI power-on so the
    // peripheral HIDS device can be installed before the controller starts.
    bool startController();

    void poll();
    bool beginDiscovery();

    // UI5K-BT-OUT1 shares the same BTstack instance for Central/Host input
    // and Peripheral/Device output. Pause only background discovery while a
    // phone/PC owns the peripheral link; existing input peers remain active.
    void setPlatformOutputLinkActive(bool active);

    // Cable Pairing Assist starts only after a known Bluetooth-capable
    // controller leaves USB. While the controller is wired, U10F USB Host
    // behavior is left completely undisturbed.
    void notifyWiredGamepadDetached(
        std::uint16_t vid,
        std::uint16_t pid
    );

    bool initialized() const {
        return initialized_;
    }

    std::size_t connectedPeerCount() const;

    BluetoothHidOutputResult sendLeOutputReport(
        std::uint16_t connectionHandle,
        std::uint8_t serviceInstance,
        std::uint8_t reportId,
        const std::uint8_t* report,
        std::size_t reportLength
    );

    void handlePacket(
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

    void handleLeHidPacket(
        std::uint8_t packetType,
        std::uint16_t channel,
        std::uint8_t* packet,
        std::uint16_t size
    );

    void handleDiscoveryTimer();

private:
    enum class DiscoveryPhase : std::uint8_t {
        Idle = 0,
        LeScan,
        ClassicInquiry,
        PausedForConnection,
    };

    enum class PendingKind : std::uint8_t {
        None = 0,
        Ble,
        Classic,
    };

    struct Peer {
        bool active = false;
        TransportType transport = TransportType::BluetoothLe;
        std::array<std::uint8_t, 6> address {};
        std::uint8_t addressType = 0;
        std::uint16_t connectionHandle = 0;
        std::uint16_t hidCid = 0;
        std::uint8_t serviceCount = 0;
    };

    Peer* allocatePeer();
    Peer* findBleByHandle(std::uint16_t connectionHandle);
    Peer* findLeByHidsCid(std::uint16_t hidsCid);
    Peer* findClassicByCid(std::uint16_t hidCid);

    bool addressConnected(const std::uint8_t* address) const;
    bool addressPending(const std::uint8_t* address) const;
    bool hasCapacity() const;

    void clearPeer(Peer& peer);
    void notifyPeerDisconnected(Peer& peer);

    void startLeScan();
    void startClassicInquiry();
    void serviceDeferredBleConnect();
    void resumeDiscovery();
    void stopDiscovery();
    void scheduleDiscoveryTimer(std::uint32_t timeoutMs);
    void stopDiscoveryTimer();

    bool advertisementHasHidServiceUuid(
        const std::uint8_t* packet
    ) const;
    void clearLegacyBondsOnce();
    void clearBleBondDatabase();

    void servicePairingAssist();
    void requestPairingAssist();
    static bool isKnownBluetoothCapableUsbGamepad(
        std::uint16_t vid,
        std::uint16_t pid
    );

    void connectLeCandidate(
        const std::uint8_t* address,
        std::uint8_t addressType
    );

    void connectClassicCandidate(
        const std::uint8_t* address
    );

    void startLeHids(std::uint16_t connectionHandle);

    BluetoothHostV2Observer* observer_ = nullptr;
    bool initialized_ = false;
    bool controllerStarted_ = false;
    bool hciWorking_ = false;
    bool platformOutputLinkActive_ = false;

    DiscoveryPhase discoveryPhase_ = DiscoveryPhase::Idle;
    PendingKind pendingKind_ = PendingKind::None;

    std::array<std::uint8_t, 6> pendingAddress_ {};
    std::uint8_t pendingAddressType_ = 0;
    std::uint16_t pendingClassicCid_ = 0;

    bool deferredBleCandidateValid_ = false;
    std::array<std::uint8_t, 6> deferredBleAddress_ {};
    std::uint8_t deferredBleAddressType_ = 0;

    bool pairingAssistRequested_ = false;
    bool pairingAssistActive_ = false;
    std::uint64_t pairingAssistUntilUs_ = 0;

    std::array<Peer, kMaxPeers> peers_ {};

    std::array<std::uint8_t, 8192> leDescriptorStorage_ {};
    std::array<std::uint8_t, 4096> classicDescriptorStorage_ {};
};

} // namespace oag::firmware
