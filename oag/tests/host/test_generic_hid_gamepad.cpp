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

    // Chinese/vendor receiver heuristic: wrong top-level collection, but
    // structurally it still exposes X/Y axes and buttons.
    const std::uint8_t vendorWrappedDescriptor[] = {
        0x06, 0x00, 0xFF, // Vendor Usage Page
        0x09, 0x01,
        0xA1, 0x01,       // Vendor top-level Application collection

        0x05, 0x01,       // Generic Desktop
        0x15, 0x00,
        0x26, 0xFF, 0x00,
        0x75, 0x08,
        0x95, 0x04,
        0x09, 0x30,       // X
        0x09, 0x31,       // Y
        0x09, 0x33,       // Rx
        0x09, 0x34,       // Ry
        0x81, 0x02,

        0x05, 0x09,       // Buttons
        0x19, 0x01,
        0x29, 0x08,
        0x15, 0x00,
        0x25, 0x01,
        0x75, 0x01,
        0x95, 0x08,
        0x81, 0x02,
        0xC0
    };

    GenericHidGamepadDescriptor rejectedVendor {};
    assert(!driver.parseDescriptor(
        vendorWrappedDescriptor,
        sizeof(vendorWrappedDescriptor),
        quirks,
        rejectedVendor
    ));

    assert(driver.looksLikeGamepadDescriptor(
        vendorWrappedDescriptor,
        sizeof(vendorWrappedDescriptor)
    ));

    GenericHidGamepadQuirks forced {};
    forced.forceGamepad = true;

    GenericHidGamepadDescriptor recoveredVendor {};
    assert(driver.parseDescriptor(
        vendorWrappedDescriptor,
        sizeof(vendorWrappedDescriptor),
        forced,
        recoveredVendor
    ));
    assert(recoveredVendor.valid);
    assert(recoveredVendor.fieldCount >= 12);


    // DragonRise / PC-Twin-Shock family signature.
    //
    // This is intentionally identified by report structure rather than by
    // VID/PID: 5 unsigned 8-bit axes, 4-bit hat, 12 one-bit buttons and an
    // 8-bit vendor-defined input bitmap in the same joystick collection.
    // That lets equivalent rebadged/generic controllers receive the same
    // normalization without changing unrelated DirectInput devices.
    const std::uint8_t twinShockFamilyDescriptor[] = {
        0x05, 0x01,       // Generic Desktop
        0x09, 0x04,       // Joystick
        0xA1, 0x01,       // Application
        0xA1, 0x02,       // Logical

        0x75, 0x08,
        0x95, 0x05,
        0x15, 0x00,
        0x26, 0xFF, 0x00,
        0x09, 0x30,       // X
        0x09, 0x34,       // Ry
        0x09, 0x32,       // Z
        0x09, 0x31,       // Y
        0x09, 0x34,       // Ry
        0x81, 0x02,

        0x75, 0x04,
        0x95, 0x01,
        0x15, 0x00,
        0x25, 0x07,
        0x09, 0x39,       // Hat
        0x81, 0x42,

        0x75, 0x01,
        0x95, 0x0C,
        0x15, 0x00,
        0x25, 0x01,
        0x05, 0x09,
        0x19, 0x01,
        0x29, 0x0C,
        0x81, 0x02,

        0x06, 0x00, 0xFF,
        0x75, 0x01,
        0x95, 0x08,
        0x15, 0x00,
        0x25, 0x01,
        0x09, 0x01,
        0x81, 0x02,

        0xC0,
        0xC0
    };

    GenericHidGamepadDescriptor twinShockParsed {};
    assert(driver.parseDescriptor(
        twinShockFamilyDescriptor,
        sizeof(twinShockFamilyDescriptor),
        quirks,
        twinShockParsed
    ));

    // Raw usage 3 is the physical South/Cross button on this family.
    const std::uint8_t twinShockSouth[] = {
        0x80, 0x80, 0x80, 0x80, 0x80,
        0x4F, // neutral hat (0xF) + usage 3
        0x00,
        0x00
    };

    UniversalGamepadState twinShockState {};
    assert(driver.parseReport(
        DeviceId {6, 1},
        twinShockParsed,
        quirks,
        twinShockSouth,
        sizeof(twinShockSouth),
        320000,
        twinShockState
    ));
    assert((twinShockState.buttons & ButtonSouth) != 0);
    assert((twinShockState.buttons & ButtonWest) == 0);

    // Raw usage 1 is physical North/Triangle.
    const std::uint8_t twinShockNorth[] = {
        0x80, 0x80, 0x80, 0x80, 0x80,
        0x1F,
        0x00,
        0x00
    };

    assert(driver.parseReport(
        DeviceId {6, 1},
        twinShockParsed,
        quirks,
        twinShockNorth,
        sizeof(twinShockNorth),
        320100,
        twinShockState
    ));
    assert((twinShockState.buttons & ButtonNorth) != 0);
    assert((twinShockState.buttons & ButtonSouth) == 0);

    // Raw usage 4 is physical West/Square.
    const std::uint8_t twinShockWest[] = {
        0x80, 0x80, 0x80, 0x80, 0x80,
        0x8F,
        0x00,
        0x00
    };

    assert(driver.parseReport(
        DeviceId {6, 1},
        twinShockParsed,
        quirks,
        twinShockWest,
        sizeof(twinShockWest),
        320200,
        twinShockState
    ));
    assert((twinShockState.buttons & ButtonWest) != 0);
    assert((twinShockState.buttons & ButtonNorth) == 0);

    // Raw usage 2 remains physical East/Circle.
    const std::uint8_t twinShockEast[] = {
        0x80, 0x80, 0x80, 0x80, 0x80,
        0x2F,
        0x00,
        0x00
    };

    assert(driver.parseReport(
        DeviceId {6, 1},
        twinShockParsed,
        quirks,
        twinShockEast,
        sizeof(twinShockEast),
        320300,
        twinShockState
    ));
    assert((twinShockState.buttons & ButtonEast) != 0);
    assert((twinShockState.buttons & ButtonWest) == 0);

    // Existing generic descriptors must NOT accidentally inherit this family
    // mapping. The original generic test above still maps usage 1 to South.

    // Known TwinShock-family classification must normalize the FINAL
    // universal face state even when the descriptor itself is a different
    // generic layout variant. This covers 0079:0006 clones that share an ID
    // but not one exact descriptor.
    GenericHidGamepadQuirks forcedTwinShock {};
    forcedTwinShock.buttonLayout =
        GenericHidButtonLayout::DragonRiseTwinShockFamily;

    GenericHidGamepadDescriptor forcedTwinShockParsed {};
    assert(driver.parseDescriptor(
        descriptor,
        sizeof(descriptor),
        forcedTwinShock,
        forcedTwinShockParsed
    ));

    // Generic raw usage 3 normally becomes ButtonWest. For this family the
    // hardware-observed physical button is Cross/South, so post-normalization
    // must rotate old West -> South.
    const std::uint8_t forcedTwinShockSouth[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F,
        0x04, 0x00
    };

    UniversalGamepadState forcedTwinShockState {};
    assert(driver.parseReport(
        DeviceId {7, 1},
        forcedTwinShockParsed,
        forcedTwinShock,
        forcedTwinShockSouth,
        sizeof(forcedTwinShockSouth),
        330000,
        forcedTwinShockState
    ));
    assert((forcedTwinShockState.buttons & ButtonSouth) != 0);
    assert((forcedTwinShockState.buttons & ButtonWest) == 0);

    // Generic raw usage 1 normally becomes South. On this family that is the
    // physical Triangle/North button, so old South -> North.
    const std::uint8_t forcedTwinShockNorth[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F,
        0x01, 0x00
    };

    assert(driver.parseReport(
        DeviceId {7, 1},
        forcedTwinShockParsed,
        forcedTwinShock,
        forcedTwinShockNorth,
        sizeof(forcedTwinShockNorth),
        330100,
        forcedTwinShockState
    ));
    assert((forcedTwinShockState.buttons & ButtonNorth) != 0);
    assert((forcedTwinShockState.buttons & ButtonSouth) == 0);

    // Generic raw usage 4 normally becomes North. On this family that is the
    // physical Square/West button, so old North -> West.
    const std::uint8_t forcedTwinShockWest[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F,
        0x08, 0x00
    };

    assert(driver.parseReport(
        DeviceId {7, 1},
        forcedTwinShockParsed,
        forcedTwinShock,
        forcedTwinShockWest,
        sizeof(forcedTwinShockWest),
        330200,
        forcedTwinShockState
    ));
    assert((forcedTwinShockState.buttons & ButtonWest) != 0);
    assert((forcedTwinShockState.buttons & ButtonNorth) == 0);

    // East/Circle remains East.
    const std::uint8_t forcedTwinShockEast[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F,
        0x02, 0x00
    };

    assert(driver.parseReport(
        DeviceId {7, 1},
        forcedTwinShockParsed,
        forcedTwinShock,
        forcedTwinShockEast,
        sizeof(forcedTwinShockEast),
        330300,
        forcedTwinShockState
    ));
    assert((forcedTwinShockState.buttons & ButtonEast) != 0);
    assert((forcedTwinShockState.buttons & ButtonWest) == 0);

    GenericHidGamepadQuirks sonyQuirks {};
    sonyQuirks.buttonLayout =
        GenericHidButtonLayout::SonyPlayStation;

    GenericHidGamepadDescriptor sonyParsed {};
    assert(driver.parseDescriptor(
        descriptor,
        sizeof(descriptor),
        sonyQuirks,
        sonyParsed
    ));

    const std::uint8_t sonyReport[] = {
        0x80, 0x80, 0x80, 0x80,
        0x0F,
        0x03, 0x01
    };

    UniversalGamepadState sonyState {};
    assert(driver.parseReport(
        DeviceId {5, 1},
        sonyParsed,
        sonyQuirks,
        sonyReport,
        sizeof(sonyReport),
        310000,
        sonyState
    ));
    assert((sonyState.buttons & ButtonWest) != 0);
    assert((sonyState.buttons & ButtonSouth) != 0);
    assert((sonyState.buttons & ButtonBack) != 0);

    return 0;
}
