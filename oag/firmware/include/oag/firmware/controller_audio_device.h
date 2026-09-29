#pragma once
#include <cstddef>
#include <cstdint>

namespace oag::firmware {

bool controllerAudioDeviceStreaming();
std::uint16_t controllerAudioDeviceWrite(
    const void* data,
    std::uint16_t length
);

} // namespace oag::firmware
