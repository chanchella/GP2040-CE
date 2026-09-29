#pragma once
#include <cstddef>
#include <cstdint>

namespace oag::audio {

struct AudioStreamDescriptor {
    bool valid = false;
    std::uint8_t interfaceNumber = 0;
    std::uint8_t alternateSetting = 0;
    std::uint8_t endpointAddress = 0;
    std::uint16_t maxPacketSize = 0;
    std::uint8_t interval = 0;
    std::uint8_t channels = 0;
    std::uint8_t bytesPerSample = 0;
    std::uint8_t bitsPerSample = 0;
    std::uint32_t sampleRate = 0;
    bool sampleRateControl = false;
};

struct ControllerAudioUsbDescriptor {
    AudioStreamDescriptor playback;
    AudioStreamDescriptor capture;
    bool xboxGipAudioInterface = false;
};

ControllerAudioUsbDescriptor parseControllerAudioUsbConfiguration(
    const std::uint8_t* bytes,
    std::size_t length
);

} // namespace oag::audio
