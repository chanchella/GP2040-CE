#include "oag/audio/controller_audio_usb_parser.h"

namespace oag::audio {
namespace {
constexpr std::uint8_t kInterface = 0x04;
constexpr std::uint8_t kEndpoint = 0x05;
constexpr std::uint8_t kCsInterface = 0x24;
constexpr std::uint8_t kCsEndpoint = 0x25;
constexpr std::uint8_t kAudioClass = 0x01;
constexpr std::uint8_t kAudioStreaming = 0x02;
constexpr std::uint8_t kVendorClass = 0xFF;
constexpr std::uint8_t kXboxSubclass = 0x47;
constexpr std::uint8_t kXboxProtocol = 0xD0;
constexpr std::uint8_t kIsochronous = 0x01;

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
        static_cast<std::uint16_t>(p[1] << 8);
}
std::uint32_t le24(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16);
}
} // namespace

ControllerAudioUsbDescriptor parseControllerAudioUsbConfiguration(
    const std::uint8_t* bytes,
    std::size_t length
) {
    ControllerAudioUsbDescriptor out {};
    if (bytes == nullptr || length < 2) {
        return out;
    }

    bool audioStreaming = false;
    std::uint8_t currentInterface = 0;
    std::uint8_t currentAlt = 0;
    std::uint8_t channels = 0;
    std::uint8_t bytesPerSample = 0;
    std::uint8_t bitsPerSample = 0;
    std::uint32_t sampleRate = 0;

    for (std::size_t offset = 0; offset + 2 <= length;) {
        const std::uint8_t len = bytes[offset];
        const std::uint8_t type = bytes[offset + 1];
        if (len < 2 || offset + len > length) {
            break;
        }
        const std::uint8_t* p = bytes + offset;

        if (type == kInterface && len >= 9) {
            currentInterface = p[2];
            currentAlt = p[3];
            const std::uint8_t cls = p[5];
            const std::uint8_t sub = p[6];
            const std::uint8_t proto = p[7];
            audioStreaming =
                cls == kAudioClass &&
                sub == kAudioStreaming &&
                currentAlt != 0;
            channels = 0;
            bytesPerSample = 0;
            bitsPerSample = 0;
            sampleRate = 0;

            if (
                currentInterface == 1 &&
                cls == kVendorClass &&
                sub == kXboxSubclass &&
                proto == kXboxProtocol
            ) {
                out.xboxGipAudioInterface = true;
            }
        } else if (
            audioStreaming &&
            type == kCsInterface &&
            len >= 11 &&
            p[2] == 0x02 &&
            p[3] == 0x01
        ) {
            channels = p[4];
            bytesPerSample = p[5];
            bitsPerSample = p[6];
            sampleRate = le24(p + 8);
        } else if (
            audioStreaming &&
            type == kEndpoint &&
            len >= 7 &&
            (p[3] & 0x03u) == kIsochronous
        ) {
            AudioStreamDescriptor stream {};
            stream.valid = true;
            stream.interfaceNumber = currentInterface;
            stream.alternateSetting = currentAlt;
            stream.endpointAddress = p[2];
            stream.maxPacketSize = static_cast<std::uint16_t>(le16(p + 4) & 0x07FFu);
            stream.interval = p[6];
            stream.channels = channels;
            stream.bytesPerSample = bytesPerSample;
            stream.bitsPerSample = bitsPerSample;
            stream.sampleRate = sampleRate;

            if ((p[2] & 0x80u) != 0) {
                out.capture = stream;
            } else {
                out.playback = stream;
            }
        } else if (
            audioStreaming &&
            type == kCsEndpoint &&
            len >= 4
        ) {
            AudioStreamDescriptor* stream = nullptr;
            if (out.capture.valid &&
                out.capture.interfaceNumber == currentInterface &&
                out.capture.alternateSetting == currentAlt) {
                stream = &out.capture;
            } else if (
                out.playback.valid &&
                out.playback.interfaceNumber == currentInterface &&
                out.playback.alternateSetting == currentAlt
            ) {
                stream = &out.playback;
            }
            if (stream != nullptr) {
                stream->sampleRateControl = (p[3] & 0x01u) != 0;
            }
        }

        offset += len;
    }
    return out;
}

} // namespace oag::audio
