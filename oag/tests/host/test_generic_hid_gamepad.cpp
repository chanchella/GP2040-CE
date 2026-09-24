#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/protocol/hid/generic_hid_gamepad_driver.h"

using namespace oag;

int main() {
    // Generic 12-button, 4-axis, hat gamepad:
    // X/Y/Rx/Ry = 8-bit 0..255
    // Hat = 4-bit 0..7 with null state
    // 12 buttons = 1 bit each
    const std::uint8_t descriptor[] = {
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x05,       // Usage (Game Pad)
        0xA1, 0x01,       // Collection (Application)

        0x15, 0x00,       // Logical Min 0
        0x26, 0xFF, 0x00, // Logical Max 255
        0x75, 0x08,       // Report Size 8
        0x95, 0x04,       // Report Count 4
        0x09, 0x30,       // X
        0x09, 0x31,       // Y
        0x09, 0x33,       // Rx
        0x09, 0x34,       // Ry
        0x81, 0x02,       // Input Data,Var,Abs

        0x15, 0x00,
        0x25, 0x07,
        0x35, 0x00,
        0x46, 0x3B, 0x01,
        0x65, 0x14,
        0x75, 0x04,
        0x95, 0x01,
        0x09, 0x39,       // Hat
        0x81, 0x42,       // Input Data,Var,Abs,Null

        0x75, 0x04,
        0x95, 0x01,
        0x81, 0x01,       // 4-bit padding

        0x05, 0x09,       // Usage Page Button
        0x19, 0x01,       // Usage Min 1
        0x29, 0x0C,       // Usage Max 12
        0x15, 0x00,
        0x25, 0x01,
        0x75, 0x01,
        0x95, 0x0C,
        0x81, 0x02,       // 12 buttons

        0x75, 0x04,
        0x95, 0x01,
        0x81, 0x01,       // padding
        0xC0
    };

    GenericHidGamepadDriver driver;
    GenericHidGamepadDescriptor parsed {};
    GenericHidGamepadQuirks quirks {};

    assert(driver.parseDescriptor(
        descriptor,
        sizeof(descriptor),
        quirks,
        parsed
    ));

    assert(parsed.valid);
    assert(parsed.isGamepad);
    assert(parsed.topUsagePage == 0x01);
    assert(parsed.topUsage == 0x05);
    assert(parsed.fieldCount >= 17);

    // Center-ish X/Y, full-right Rx, full-up Ry.
    // Hat=2 (Right), buttons 1 + 4 + 10.
    const std::uint8_t report[] = {
        0x80, 0x80, 0xFF, 0x00,
        0x02,
        0x09, 0x02
    };

    UniversalGamepadState state {};
    const DeviceId source {1, 1};

    assert(driver.parseReport(
        source,
        parsed,
        quirks,
        report,
        sizeof(report),
        123456,
        state
    ));

    assert(state.connected);
    assert(state.source == source);
    assert(state.generation == 1);
    assert(state.timestampUs == 123456);

    assert(state.lx > -20000000 && state.lx < 20000000);
    assert(state.ly > -20000000 && state.ly < 20000000);
    assert(state.rx == std::numeric_limits<std::int32_t>::max());
    assert(state.ry == std::numeric_limits<std::int32_t>::min());

    assert(
        state.dpad ==
        static_cast<std::uint8_t>(DpadBits::Right)
    );

    assert((state.buttons & ButtonSouth) != 0);
    assert((state.buttons & ButtonNorth) != 0);
    assert((state.buttons & ButtonStart) != 0);

    // Neutral/release report must still publish after the first state.
    const std::uint8_t neutral[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F, // Null/out-of-range hat -> neutral
        0x00, 0x00
    };

    assert(driver.parseReport(
        source,
        parsed,
        quirks,
        neutral,
        sizeof(neutral),
        123999,
        state
    ));

    assert(state.generation == 2);
    assert(state.buttons == 0);
    assert(state.dpad == 0);

    // Report-ID support.
    const std::uint8_t reportIdDescriptor[] = {
        0x05, 0x01,
        0x09, 0x05,
        0xA1, 0x01,
        0x85, 0x03,       // Report ID 3
        0x15, 0x81,       // -127
        0x25, 0x7F,       // +127
        0x75, 0x08,
        0x95, 0x02,
        0x09, 0x30,
        0x09, 0x31,
        0x81, 0x02,
        0xC0
    };

    GenericHidGamepadDescriptor withId {};
    assert(driver.parseDescriptor(
        reportIdDescriptor,
        sizeof(reportIdDescriptor),
        quirks,
        withId
    ));
    assert(withId.usesReportIds);

    const std::uint8_t idReport[] = {
        0x03, 0x81, 0x7F
    };

    UniversalGamepadState idState {};
    assert(driver.parseReport(
        DeviceId {2, 1},
        withId,
        quirks,
        idReport,
        sizeof(idReport),
        200000,
        idState
    ));
    assert(idState.lx == std::numeric_limits<std::int32_t>::min());
    assert(idState.ly == std::numeric_limits<std::int32_t>::max());

    return 0;
}
