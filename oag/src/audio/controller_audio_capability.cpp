#include "oag/audio/controller_audio_capability.h"

namespace oag::audio {
namespace {

bool sonyAudioController(std::uint16_t vid, std::uint16_t pid) {
    if (vid != 0x054C) {
        return false;
    }

    switch (pid) {
        case 0x05C4: // DualShock 4 v1
        case 0x09CC: // DualShock 4 v2
        case 0x0CE6: // DualSense
        case 0x0DF2: // DualSense Edge
            return true;
        default:
            return false;
    }
}

bool microsoftModernController(std::uint16_t vid, std::uint16_t pid) {
    if (vid != 0x045E) {
        return false;
    }

    switch (pid) {
        case 0x02D1:
        case 0x02DD:
        case 0x02E3:
        case 0x02EA:
        case 0x0B00:
        case 0x0B0A:
        case 0x0B12:
            return true;
        default:
            return false;
    }
}

} // namespace

ControllerAudioCapability classifyControllerAudio(
    std::uint16_t vid,
    std::uint16_t pid,
    const ControllerAudioUsbShape& usb
) {
    if (
        sonyAudioController(vid, pid) &&
        (usb.hasUac1StreamingIn || usb.hasUac1StreamingOut)
    ) {
        return {
            ControllerAudioTransport::UsbUac1,
            usb.hasUac1StreamingOut,
            usb.hasUac1StreamingIn,
            true,
        };
    }

    if (
        microsoftModernController(vid, pid) &&
        usb.hasXboxGipAudioInterface
    ) {
        return {
            ControllerAudioTransport::XboxGipIsochronous,
            true,
            true,
            true,
        };
    }

    return {};
}

} // namespace oag::audio
