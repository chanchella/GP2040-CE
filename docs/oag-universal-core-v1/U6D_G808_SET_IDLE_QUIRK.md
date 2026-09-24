# U6D — Golden G808 Enumeration Quirk

U6D restores the exact host-side behavior that previously allowed the
Redragon G808 receiver to enumerate reliably in the Golden firmware.

## Problem

The G808 receiver identified as 2563:0575 can leave its usable controller mode
when the HID host sends SET_IDLE during enumeration. If this happens, the
higher-level descriptor/gamepad parser never gets a usable device.

## Fix

The dependency preparation step now patches the pinned TinyUSB HID host
configuration state machine so that only 2563:0575 skips SET_IDLE.

After the skip, TinyUSB continues the normal next configuration step:

- CONFIG_SET_PROTOCOL for boot-capable interfaces; or
- CONFIG_GET_REPORT_DESC otherwise.

The patch is guarded by:

1. exact TinyUSB dependency SHA verification;
2. exact source-block precondition;
3. post-patch content verification.

If any precondition changes, the build fails instead of applying a guessed
patch.

## Preserved U6 behavior

- USB hub support and 12-device host budget.
- 8 logical gamepad input slots.
- Generic HID structural fallback.
- known G808/GigaMax/Shanwan classifier.
- wired Xbox One/Series XGIP input/init.
- T29/XUSB input and reverse rumble.
- keyboard + mouse controller mapping.
- standard keyboard lock LED output.
- target-facing PC Xbox 360/XInput profile unchanged.
