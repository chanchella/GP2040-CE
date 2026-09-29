#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "oag/audio/controller_audio_usb_parser.h"
#include "tusb.h"

namespace oag::firmware {
class ControllerAudioHost {
public:
    void onConfigurationDescriptor(std::uint8_t devAddr, const std::uint8_t* descriptor, std::size_t length);
    void onMounted(std::uint8_t devAddr);
    void onUnmounted(std::uint8_t devAddr);
    bool captureActive() const { return captureActive_; }
    std::uint32_t capturedPackets() const { return capturedPackets_; }
    std::uint32_t capturedBytes() const { return capturedBytes_; }
private:
    static constexpr std::size_t kMaxDevices = CFG_TUH_DEVICE_MAX + 1;
    static constexpr std::size_t kTransferBytes = 1024;
    static void interfaceSetThunk(tuh_xfer_t* xfer);
    static void captureThunk(tuh_xfer_t* xfer);
    void onInterfaceSet(tuh_xfer_t* xfer);
    void onCapture(tuh_xfer_t* xfer);
    bool queueCapture();
    std::array<oag::audio::ControllerAudioUsbDescriptor, kMaxDevices> descriptors_ {};
    std::array<std::uint8_t, kTransferBytes> captureBuffer_ {};
    std::uint8_t activeDevAddr_ = 0;
    std::uint8_t captureEndpoint_ = 0;
    bool captureEndpointOpen_ = false;
    bool captureActive_ = false;
    std::uint32_t capturedPackets_ = 0;
    std::uint32_t capturedBytes_ = 0;
};
} // namespace oag::firmware
