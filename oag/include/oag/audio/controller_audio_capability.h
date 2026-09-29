#pragma once

#include <cstdint>

namespace oag::audio {

enum class ControllerAudioTransport : std::uint8_t {
    None = 0,
    UsbUac1,
    XboxGipIsochronous,
};

struct ControllerAudioUsbShape {
    bool hasUac1StreamingIn = false;
    bool hasUac1StreamingOut = false;
    bool hasXboxGipAudioInterface = false;
};

struct ControllerAudioCapability {
    ControllerAudioTransport transport = ControllerAudioTransport::None;
    bool playback = false;
    bool capture = false;
    bool controllerJack = false;
};

ControllerAudioCapability classifyControllerAudio(
    std::uint16_t vid,
    std::uint16_t pid,
    const ControllerAudioUsbShape& usb
);

} // namespace oag::audio
