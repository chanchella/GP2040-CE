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
    void poll();
    bool beginDiscovery();

    // Physical USB attachment of a known Bluetooth-capable controller is an
    // explicit recovery gesture. It never fabricates a Bluetooth identity;
    // instead it repairs stale bonding state and immediately prioritizes BLE
    // discovery so the same controller can re-pair as soon as it advertises.
    void notifyWiredGamepadAttached(
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

    void serviceWiredPairingAssist();
    bool deleteRememberedStaleLeBond();
    bool deleteSingleStoredLeBondForAssist();
    void rememberStaleLeBond(
        const std::uint8_t* address,
        std::uint8_t addressType
    );
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
    bool hciWorking_ = false;

    DiscoveryPhase discoveryPhase_ = DiscoveryPhase::Idle;
    PendingKind pendingKind_ = PendingKind::None;

    std::array<std::uint8_t, 6> pendingAddress_ {};
    std::uint8_t pendingAddressType_ = 0;
    std::uint16_t pendingClassicCid_ = 0;

    bool deferredBleCandidateValid_ = false;
    std::array<std::uint8_t, 6> deferredBleAddress_ {};
    std::uint8_t deferredBleAddressType_ = 0;

    bool wiredPairingAssistPending_ = false;
    bool bleRecoveryPriorityActive_ = false;
    std::uint64_t bleRecoveryPriorityUntilUs_ = 0;

    bool staleLeBondValid_ = false;
    std::array<std::uint8_t, 6> staleLeBondAddress_ {};
    std::uint8_t staleLeBondAddressType_ = 0;

    std::array<Peer, kMaxPeers> peers_ {};

    std::array<std::uint8_t, 8192> leDescriptorStorage_ {};
    std::array<std::uint8_t, 4096> classicDescriptorStorage_ {};
};

} // namespace oag::firmware
