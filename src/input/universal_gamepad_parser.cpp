#include "input/universal_gamepad_parser.h"

UniversalGamepadParser& UniversalGamepadParser::getInstance() {
    static UniversalGamepadParser instance;
    return instance;
}

bool UniversalGamepadParser::parse(
    UniversalDeviceMatch const& device,
    uint8_t const* report,
    uint16_t len,
    GamepadState& out
) const {
    switch (device.protocol) {
        case UniversalProtocol::XUSB_XBOX360:
            return parseXusbXbox360(device, report, len, out);

        // Registered now, implemented in later controller-input phases.
        case UniversalProtocol::HID_GAMEPAD:
        case UniversalProtocol::SONY_DS3:
        case UniversalProtocol::SONY_DS4:
        case UniversalProtocol::SONY_DUALSENSE:
        case UniversalProtocol::XGIP_XBOX_ONE:
        case UniversalProtocol::XID_XBOX_ORIGINAL:
        case UniversalProtocol::NINTENDO_SWITCH_PRO:
        case UniversalProtocol::XBOX360_WIRELESS_RECEIVER:
        case UniversalProtocol::XUSB_AUXILIARY:
        case UniversalProtocol::HID_GENERIC:
        case UniversalProtocol::HID_KEYBOARD:
        case UniversalProtocol::HID_MOUSE:
        case UniversalProtocol::VENDOR_SPECIFIC:
        case UniversalProtocol::UNKNOWN:
        default:
            return false;
    }
}

uint16_t UniversalGamepadParser::axisX(int16_t value) {
    return static_cast<uint16_t>(
        static_cast<int32_t>(value) + 32768
    );
}

uint16_t UniversalGamepadParser::axisY(int16_t value) {
    // GP2040's XInput device output driver inverts Y.
    // Keep normalized internal orientation consistent with the
    // previously hardware-proven T29 path.
    return static_cast<uint16_t>(
        32767 - static_cast<int32_t>(value)
    );
}

bool UniversalGamepadParser::parseXusbXbox360(
    UniversalDeviceMatch const& device,
    uint8_t const* report,
    uint16_t len,
    GamepadState& out
) const {
    if (report == nullptr || len < 14) {
        return false;
    }

    // Xbox 360-compatible gameplay report.
    //
    // The 045E:028E-compatible path is known to vary byte 0 on some
    // devices, therefore the registry carries STATUS_BYTE_VARIANT and
    // the proven parser validates the report-length byte at report[1].
    if (report[1] != 0x14) {
        return false;
    }

    (void)device;

    const uint16_t buttons =
        static_cast<uint16_t>(report[2]) |
        (static_cast<uint16_t>(report[3]) << 8);

    if (buttons & 0x0001) out.dpad |= GAMEPAD_MASK_UP;
    if (buttons & 0x0002) out.dpad |= GAMEPAD_MASK_DOWN;
    if (buttons & 0x0004) out.dpad |= GAMEPAD_MASK_LEFT;
    if (buttons & 0x0008) out.dpad |= GAMEPAD_MASK_RIGHT;

    if (buttons & 0x0010) out.buttons |= GAMEPAD_MASK_S2;
    if (buttons & 0x0020) out.buttons |= GAMEPAD_MASK_S1;
    if (buttons & 0x0040) out.buttons |= GAMEPAD_MASK_L3;
    if (buttons & 0x0080) out.buttons |= GAMEPAD_MASK_R3;
    if (buttons & 0x0100) out.buttons |= GAMEPAD_MASK_L1;
    if (buttons & 0x0200) out.buttons |= GAMEPAD_MASK_R1;
    if (buttons & 0x0400) out.buttons |= GAMEPAD_MASK_A1;

    if (buttons & 0x1000) out.buttons |= GAMEPAD_MASK_B1;
    if (buttons & 0x2000) out.buttons |= GAMEPAD_MASK_B2;
    if (buttons & 0x4000) out.buttons |= GAMEPAD_MASK_B3;
    if (buttons & 0x8000) out.buttons |= GAMEPAD_MASK_B4;

    out.lt = report[4];
    out.rt = report[5];

    if (out.lt != 0) out.buttons |= GAMEPAD_MASK_L2;
    if (out.rt != 0) out.buttons |= GAMEPAD_MASK_R2;

    auto readS16 = [report](uint8_t offset) -> int16_t {
        const uint16_t raw =
            static_cast<uint16_t>(report[offset]) |
            (static_cast<uint16_t>(report[offset + 1]) << 8);

        return static_cast<int16_t>(raw);
    };

    out.lx = axisX(readS16(6));
    out.ly = axisY(readS16(8));
    out.rx = axisX(readS16(10));
    out.ry = axisY(readS16(12));
    out.dpadOriginal = out.dpad;

    return true;
}
