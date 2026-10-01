#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace oag {
inline constexpr std::uint8_t kNativeMouseReportDescriptor[] = {
    0x05,0x01,0x09,0x02,0xA1,0x01,0x09,0x01,0xA1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,
    0x95,0x01,0x75,0x03,0x81,0x01,
    0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x00,0x80,0x26,0xFF,0x7F,0x75,0x10,0x95,0x02,0x81,0x06,
    0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,
    0x05,0x0C,0x0A,0x38,0x02,0x81,0x06,0xC0,0xC0
};
struct NativeMousePacket {
    std::array<std::uint8_t, 7> bytes {};
    std::uint16_t length = 7;
    std::int32_t x = 0, y = 0, wheel = 0, pan = 0;
};
class NativeMouseQueue {
public:
    void add(std::int32_t x, std::int32_t y, std::int16_t wheel, std::int16_t pan) {
        x_ = bounded(x_, x); y_ = bounded(y_, y); wheel_ = bounded(wheel_, wheel); pan_ = bounded(pan_, pan);
    }
    bool pending() const { return x_ || y_ || wheel_ || pan_; }
    void clear() { x_ = y_ = wheel_ = pan_ = 0; }
    NativeMousePacket packet(std::uint8_t buttons, bool boot) const {
        NativeMousePacket p;
        p.x = static_cast<std::int32_t>(std::clamp<std::int64_t>(x_, boot ? -127 : -32768, boot ? 127 : 32767));
        p.y = static_cast<std::int32_t>(std::clamp<std::int64_t>(y_, boot ? -127 : -32768, boot ? 127 : 32767));
        p.bytes[0] = buttons & (boot ? 7 : 31);
        if (boot) { p.length = 3; p.bytes[1] = p.x; p.bytes[2] = p.y; }
        else {
            p.bytes[1] = p.x; p.bytes[2] = static_cast<std::uint16_t>(p.x) >> 8;
            p.bytes[3] = p.y; p.bytes[4] = static_cast<std::uint16_t>(p.y) >> 8;
            p.wheel = static_cast<std::int32_t>(std::clamp<std::int64_t>(wheel_, -127, 127));
            p.pan = static_cast<std::int32_t>(std::clamp<std::int64_t>(pan_, -127, 127));
            p.bytes[5] = p.wheel; p.bytes[6] = p.pan;
        }
        return p;
    }
    void sent(const NativeMousePacket& p) {
        x_ -= p.x; y_ -= p.y;
        if (p.length == 3) wheel_ = pan_ = 0;
        else { wheel_ -= p.wheel; pan_ -= p.pan; }
    }
private:
    static std::int64_t bounded(std::int64_t a, std::int64_t b) {
        constexpr std::int64_t limit = std::int64_t(1) << 50;
        return std::clamp(a + std::clamp(b, -limit, limit), -limit, limit);
    }
    std::int64_t x_ = 0, y_ = 0, wheel_ = 0, pan_ = 0;
};
} // namespace oag
