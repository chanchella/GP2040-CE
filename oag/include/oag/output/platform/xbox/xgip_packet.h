#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oag {

enum class XgipCommand : std::uint8_t {
    AckResponse = 0x01,
    Announce = 0x02,
    KeepAlive = 0x03,
    DeviceDescriptor = 0x04,
    PowerModeDeviceConfig = 0x05,
    Auth = 0x06,
    VirtualKeycode = 0x07,
    Rumble = 0x09,
    Led = 0x0A,
    FinalAuth = 0x1E,
    InputReport = 0x20,
    HidReport = 0x21,
};

struct XgipHeader {
    std::uint8_t command = 0;
    std::uint8_t client = 0;
    bool needsAck = false;
    bool internal = false;
    bool chunkStart = false;
    bool chunked = false;
    std::uint8_t sequence = 0;
    std::uint8_t length = 0;
};

struct XgipPacketView {
    XgipHeader header {};
    const std::uint8_t* payload = nullptr;
    std::size_t payloadLength = 0;
};

class XgipPacketCodec {
public:
    bool parse(
        const std::uint8_t* packet,
        std::size_t length,
        XgipPacketView& output
    ) const;

    static void writeHeader(
        std::uint8_t* destination,
        XgipCommand command,
        std::uint8_t sequence,
        std::uint8_t payloadLength,
        bool internal,
        bool needsAck = false,
        bool chunkStart = false,
        bool chunked = false,
        std::uint8_t client = 0
    );
};

} // namespace oag
