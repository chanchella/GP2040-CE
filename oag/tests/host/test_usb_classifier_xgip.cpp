#include <cassert>
#include <cstdint>
#include <limits>

#include "oag/device/usb_device_classifier.h"
#include "oag/input/gamepad_state.h"
#include "oag/protocol/xgip/xgip_input_driver.h"

using namespace oag;

namespace {

void writeLe16(
    std::uint8_t* dst,
    std::uint16_t value
) {
    dst[0] = static_cast<std::uint8_t>(value & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

} // namespace

int main() {
    UsbDeviceClassifier classifier;

    const UsbDeviceClassification g808 = classifier.classify({
        0x2563, 0x0575, 0x03, 0x00, 0x00, 2
    });
    assert(g808.recognized);
    assert(g808.protocol == ProtocolKind::HidGamepad);
    assert(g808.driver == UsbDriverFamily::Hid);
    assert(g808.profile == UsbDeviceProfile::RedragonG808);
    assert(g808.hasQuirk(UsbQuirkForceHidGamepad));
    assert(g808.hasQuirk(UsbQuirkZRzAsRightStick));
    assert(g808.hasQuirk(UsbQuirkSkipSetIdle));

    const UsbDeviceClassification gigamax = classifier.classify({
        0x0079, 0x0006, 0x03, 0x00, 0x00, 2
    });
    assert(gigamax.recognized);
    assert(gigamax.protocol == ProtocolKind::HidGamepad);
    assert(gigamax.hasQuirk(UsbQuirkForceHidGamepad));

    const UsbDeviceClassification xusb = classifier.classify({
        0x9999, 0x1111, 0xFF, 0x5D, 0x01, 2
    });
    assert(xusb.recognized);
    assert(xusb.protocol == ProtocolKind::XusbXbox360);
    assert(xusb.driver == UsbDriverFamily::Xinput);

    const UsbDeviceClassification xgip = classifier.classify({
        0x9999, 0x2222, 0xFF, 0x47, 0xD0, 2
    });
    assert(xgip.recognized);
    assert(xgip.protocol == ProtocolKind::XgipXboxOne);
    assert(xgip.driver == UsbDriverFamily::Xinput);

    const UsbDeviceClassification xboxOneS = classifier.classify({
        0x045E, 0x02EA, 0xFF, 0x47, 0xD0, 2
    });
    assert(xboxOneS.profile == UsbDeviceProfile::XboxOneS045e02ea);
    assert(xboxOneS.protocol == ProtocolKind::XgipXboxOne);

    XgipInputDriver parser;
    UniversalGamepadState state {};

    std::uint8_t report[18] {};
    report[0] = 0x20;

    // Start + Back + A/B/X/Y + Up/Right + LB/RB/L3/R3.
    const std::uint16_t buttons =
        (1u << 2) |
        (1u << 3) |
        (1u << 4) |
        (1u << 5) |
        (1u << 6) |
        (1u << 7) |
        (1u << 8) |
        (1u << 11) |
        (1u << 12) |
        (1u << 13) |
        (1u << 14) |
        (1u << 15);

    writeLe16(&report[4], buttons);
    writeLe16(&report[6], 0x03FF);
    writeLe16(&report[8], 0x0200);

    writeLe16(
        &report[10],
        static_cast<std::uint16_t>(
            std::numeric_limits<std::int16_t>::min()
        )
    );
    writeLe16(
        &report[12],
        static_cast<std::uint16_t>(
            std::numeric_limits<std::int16_t>::max()
        )
    );
    writeLe16(
        &report[14],
        static_cast<std::uint16_t>(
            std::numeric_limits<std::int16_t>::max()
        )
    );
    writeLe16(
        &report[16],
        static_cast<std::uint16_t>(
            std::numeric_limits<std::int16_t>::min()
        )
    );

    const DeviceId source {3, 4};
    assert(parser.parse(
        source,
        report,
        sizeof(report),
        555000,
        state
    ));

    assert(state.connected);
    assert(state.source == source);
    assert(state.generation == 1);
    assert(state.timestampUs == 555000);

    assert((state.buttons & ButtonStart) != 0);
    assert((state.buttons & ButtonBack) != 0);
    assert((state.buttons & ButtonSouth) != 0);
    assert((state.buttons & ButtonEast) != 0);
    assert((state.buttons & ButtonWest) != 0);
    assert((state.buttons & ButtonNorth) != 0);
    assert((state.buttons & ButtonLeftBumper) != 0);
    assert((state.buttons & ButtonRightBumper) != 0);
    assert((state.buttons & ButtonLeftStick) != 0);
    assert((state.buttons & ButtonRightStick) != 0);

    assert(
        state.dpad ==
        (
            static_cast<std::uint8_t>(DpadBits::Up) |
            static_cast<std::uint8_t>(DpadBits::Right)
        )
    );

    assert(state.leftTrigger ==
        std::numeric_limits<std::uint32_t>::max());
    assert(state.rightTrigger > 0x7FFFFFFFu);
    assert(state.rightTrigger < 0x81000000u);

    assert(state.lx == std::numeric_limits<std::int32_t>::min());
    assert(state.ly == std::numeric_limits<std::int32_t>::min());
    assert(state.rx == std::numeric_limits<std::int32_t>::max());
    assert(state.ry == std::numeric_limits<std::int32_t>::max());

    report[0] = 0x07;
    assert(!parser.parse(
        source,
        report,
        sizeof(report),
        556000,
        state
    ));

    return 0;
}
