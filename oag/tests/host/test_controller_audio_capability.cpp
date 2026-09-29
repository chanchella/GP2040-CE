#include <cassert>
#include "oag/audio/controller_audio_capability.h"

int main() {
    using namespace oag::audio;

    ControllerAudioUsbShape dualSense {};
    dualSense.hasUac1StreamingIn = true;
    dualSense.hasUac1StreamingOut = true;

    const auto ds = classifyControllerAudio(0x054C, 0x0CE6, dualSense);
    assert(ds.transport == ControllerAudioTransport::UsbUac1);
    assert(ds.playback && ds.capture && ds.controllerJack);

    ControllerAudioUsbShape ds4 {};
    ds4.hasUac1StreamingIn = true;
    ds4.hasUac1StreamingOut = true;
    const auto p4 = classifyControllerAudio(0x054C, 0x09CC, ds4);
    assert(p4.transport == ControllerAudioTransport::UsbUac1);
    assert(p4.playback && p4.capture);

    ControllerAudioUsbShape xbox {};
    xbox.hasXboxGipAudioInterface = true;
    const auto xb = classifyControllerAudio(0x045E, 0x0B12, xbox);
    assert(xb.transport == ControllerAudioTransport::XboxGipIsochronous);
    assert(xb.playback && xb.capture && xb.controllerJack);

    ControllerAudioUsbShape noAudio {};
    const auto safe = classifyControllerAudio(0x054C, 0x0CE6, noAudio);
    assert(safe.transport == ControllerAudioTransport::None);

    const auto unknown = classifyControllerAudio(0x1234, 0x5678, dualSense);
    assert(unknown.transport == ControllerAudioTransport::None);
    return 0;
}
