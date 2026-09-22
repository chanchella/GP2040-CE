#ifndef _UNIVERSAL_GAMEPAD_PARSER_H_
#define _UNIVERSAL_GAMEPAD_PARSER_H_

#include <stdint.h>

#include "gamepad/GamepadState.h"
#include "input/universal_device_registry.h"

// Protocol-normalization layer.
//
// USB/Bluetooth transports hand raw controller reports to this class.
// The parser converts protocol-specific bytes into GP2040's normalized
// GamepadState without knowing anything about output/platform routing.
//
// G2C0B starts with the proven XUSB parser. Additional parsers
// (Generic HID, DS3, DS4, DualSense, XGIP, etc.) plug in here later.
class UniversalGamepadParser {
public:
    UniversalGamepadParser(UniversalGamepadParser const&) = delete;
    void operator=(UniversalGamepadParser const&) = delete;

    static UniversalGamepadParser& getInstance();

    bool parse(
        UniversalDeviceMatch const& device,
        uint8_t const* report,
        uint16_t len,
        GamepadState& out
    ) const;

private:
    UniversalGamepadParser() = default;

    static uint16_t axisX(int16_t value);
    static uint16_t axisY(int16_t value);

    bool parseXusbXbox360(
        UniversalDeviceMatch const& device,
        uint8_t const* report,
        uint16_t len,
        GamepadState& out
    ) const;

    bool parseXgipXboxOne(
        UniversalDeviceMatch const& device,
        uint8_t const* report,
        uint16_t len,
        GamepadState& out
    ) const;
};

#define UGAMEPADPARSER UniversalGamepadParser::getInstance()

#endif
