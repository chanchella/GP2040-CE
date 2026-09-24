#include "oag/output/platform/xbox/xgip_packet.h"

namespace oag {

bool XgipPacketCodec::parse(
    const std::uint8_t* packet,
    std::size_t length,
    XgipPacketView& output
) const {
    output = {};

    if (packet == nullptr || length < 4) {
        return false;
    }

    const std::uint8_t flags = packet[1];

    output.header.command = packet[0];
    output.header.client =
        static_cast<std::uint8_t>(flags & 0x0Fu);
    output.header.needsAck = (flags & 0x10u) != 0;
    output.header.internal = (flags & 0x20u) != 0;
    output.header.chunkStart = (flags & 0x40u) != 0;
    output.header.chunked = (flags & 0x80u) != 0;
    output.header.sequence = packet[2];
    output.header.length = packet[3];

    if (output.header.chunked) {
        // Chunked XGIP adds two chunk-position bytes before payload.
        if (length < 6) {
            return false;
        }

        const std::size_t encoded =
            static_cast<std::size_t>(
                output.header.length & 0x7Fu
            );

        if (length < 6u + encoded) {
            return false;
        }

        output.payload = packet + 6;
        output.payloadLength = encoded;
        return true;
    }

    if (length < 4u + output.header.length) {
        return false;
    }

    output.payload = packet + 4;
    output.payloadLength = output.header.length;
    return true;
}

void XgipPacketCodec::writeHeader(
    std::uint8_t* destination,
    XgipCommand command,
    std::uint8_t sequence,
    std::uint8_t payloadLength,
    bool internal,
    bool needsAck,
    bool chunkStart,
    bool chunked,
    std::uint8_t client
) {
    destination[0] = static_cast<std::uint8_t>(command);

    std::uint8_t flags =
        static_cast<std::uint8_t>(client & 0x0Fu);

    if (needsAck) flags |= 0x10u;
    if (internal) flags |= 0x20u;
    if (chunkStart) flags |= 0x40u;
    if (chunked) flags |= 0x80u;

    destination[1] = flags;
    destination[2] = sequence;
    destination[3] = payloadLength;
}

} // namespace oag
