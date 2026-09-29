#include "oag/firmware/controller_audio_host.h"
#include <algorithm>
#include "host/usbh.h"

namespace oag::firmware {
namespace {
ControllerAudioHost* gControllerAudioHost = nullptr;
bool supportedSony(std::uint16_t vid, std::uint16_t pid) {
    if (vid != 0x054C) return false;
    return pid == 0x05C4 || pid == 0x09CC || pid == 0x0CE6 || pid == 0x0DF2;
}
}

void ControllerAudioHost::onConfigurationDescriptor(std::uint8_t devAddr, const std::uint8_t* descriptor, std::size_t length) {
    if (devAddr >= descriptors_.size()) return;
    descriptors_[devAddr] = oag::audio::parseControllerAudioUsbConfiguration(descriptor, length);
}

void ControllerAudioHost::onMounted(std::uint8_t devAddr) {
    if (devAddr >= descriptors_.size() || activeDevAddr_ != 0) return;
    std::uint16_t vid = 0, pid = 0;
    if (!tuh_vid_pid_get(devAddr, &vid, &pid) || !supportedSony(vid, pid)) return;
    const auto& stream = descriptors_[devAddr].capture;
    if (!stream.valid || (stream.endpointAddress & 0x80u) == 0) return;
    gControllerAudioHost = this;
    activeDevAddr_ = devAddr;
    captureEndpoint_ = stream.endpointAddress;
    captureEndpointOpen_ = false;
    captureActive_ = false;
    capturedPackets_ = 0;
    capturedBytes_ = 0;
    if (!tuh_interface_set(devAddr, stream.interfaceNumber, stream.alternateSetting, &ControllerAudioHost::interfaceSetThunk, 0)) {
        activeDevAddr_ = 0;
        captureEndpoint_ = 0;
    }
}

void ControllerAudioHost::onUnmounted(std::uint8_t devAddr) {
    if (devAddr < descriptors_.size()) descriptors_[devAddr] = {};
    if (devAddr != activeDevAddr_) return;
    activeDevAddr_ = 0;
    captureEndpoint_ = 0;
    captureEndpointOpen_ = false;
    captureActive_ = false;
    if (gControllerAudioHost == this) gControllerAudioHost = nullptr;
}

void ControllerAudioHost::interfaceSetThunk(tuh_xfer_t* xfer) {
    if (gControllerAudioHost) gControllerAudioHost->onInterfaceSet(xfer);
}
void ControllerAudioHost::captureThunk(tuh_xfer_t* xfer) {
    if (gControllerAudioHost) gControllerAudioHost->onCapture(xfer);
}

void ControllerAudioHost::onInterfaceSet(tuh_xfer_t* xfer) {
    if (!xfer || activeDevAddr_ == 0 || xfer->daddr != activeDevAddr_ || xfer->result != XFER_RESULT_SUCCESS) {
        captureActive_ = false;
        return;
    }
    const auto& stream = descriptors_[activeDevAddr_].capture;
    tusb_desc_endpoint_t endpoint {};
    endpoint.bLength = sizeof(tusb_desc_endpoint_t);
    endpoint.bDescriptorType = TUSB_DESC_ENDPOINT;
    endpoint.bEndpointAddress = stream.endpointAddress;
    endpoint.bmAttributes.xfer = TUSB_XFER_ISOCHRONOUS;
    endpoint.bmAttributes.sync = 1;
    endpoint.bmAttributes.usage = 0;
    endpoint.wMaxPacketSize = stream.maxPacketSize;
    endpoint.bInterval = stream.interval;
    if (!tuh_edpt_open(activeDevAddr_, &endpoint)) {
        captureActive_ = false;
        return;
    }
    captureEndpointOpen_ = true;
    captureActive_ = queueCapture();
}

bool ControllerAudioHost::queueCapture() {
    if (!captureEndpointOpen_ || activeDevAddr_ == 0) return false;
    tuh_xfer_t xfer {};
    xfer.daddr = activeDevAddr_;
    xfer.ep_addr = captureEndpoint_;
    xfer.buffer = captureBuffer_.data();
    xfer.buflen = static_cast<std::uint16_t>(captureBuffer_.size());
    xfer.complete_cb = &ControllerAudioHost::captureThunk;
    return tuh_edpt_xfer(&xfer);
}

void ControllerAudioHost::onCapture(tuh_xfer_t* xfer) {
    if (!xfer || xfer->daddr != activeDevAddr_ || xfer->ep_addr != captureEndpoint_) return;
    if (xfer->result == XFER_RESULT_SUCCESS) {
        std::size_t pos = 0;
        const std::size_t total = std::min<std::size_t>(xfer->actual_len, captureBuffer_.size());
        while (pos + 2 <= total) {
            const std::uint16_t n = static_cast<std::uint16_t>(captureBuffer_[pos]) |
                static_cast<std::uint16_t>(captureBuffer_[pos + 1] << 8);
            pos += 2;
            if (n == 0xFFFFu) continue;
            if (pos + n > total) break;
            ++capturedPackets_;
            capturedBytes_ += n;
            pos += n;
        }
    }
    captureActive_ = queueCapture();
}
} // namespace oag::firmware
