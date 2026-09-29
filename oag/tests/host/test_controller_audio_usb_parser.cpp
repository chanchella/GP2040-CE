#include <cassert>
#include <cstdint>
#include "oag/audio/controller_audio_usb_parser.h"

int main() {
    using namespace oag::audio;

    // Minimal descriptor sequence shaped from the real DualSense FS UAC1
    // configuration: playback 4ch/48k/16-bit EP01, capture 2ch/48k/16-bit EP82.
    const std::uint8_t dualSense[] = {
        9,4,1,1,1,1,2,0,0,
        11,0x24,2,1,4,2,16,1,0x80,0xBB,0x00,
        7,5,0x01,0x09,0x88,0x01,1,
        7,0x25,1,1,0,0,0,
        9,4,2,1,1,1,2,0,0,
        11,0x24,2,1,2,2,16,1,0x80,0xBB,0x00,
        7,5,0x82,0x05,0xC4,0x00,1,
        7,0x25,1,0,0,0,0,
    };
    const auto ds = parseControllerAudioUsbConfiguration(dualSense, sizeof(dualSense));
    assert(ds.playback.valid && ds.capture.valid);
    assert(ds.playback.endpointAddress == 0x01);
    assert(ds.playback.maxPacketSize == 392);
    assert(ds.playback.channels == 4 && ds.playback.sampleRate == 48000);
    assert(ds.playback.sampleRateControl);
    assert(ds.capture.endpointAddress == 0x82);
    assert(ds.capture.maxPacketSize == 196);
    assert(ds.capture.channels == 2 && ds.capture.sampleRate == 48000);

    const std::uint8_t ds4[] = {
        9,4,1,1,1,1,2,0,0,
        11,0x24,2,1,2,2,16,1,0x00,0x7D,0x00,
        7,5,0x01,0x09,0x84,0x00,1,
        7,0x25,1,0,0,0,0,
        9,4,2,1,1,1,2,0,0,
        11,0x24,2,1,1,2,16,1,0x80,0x3E,0x00,
        7,5,0x82,0x05,0x22,0x00,1,
        7,0x25,1,0,0,0,0,
    };
    const auto p4 = parseControllerAudioUsbConfiguration(ds4, sizeof(ds4));
    assert(p4.playback.maxPacketSize == 132);
    assert(p4.playback.sampleRate == 32000);
    assert(p4.capture.maxPacketSize == 34);
    assert(p4.capture.sampleRate == 16000);

    const std::uint8_t xbox[] = {9,4,1,0,2,0xFF,0x47,0xD0,0};
    const auto xb = parseControllerAudioUsbConfiguration(xbox, sizeof(xbox));
    assert(xb.xboxGipAudioInterface);
    return 0;
}
