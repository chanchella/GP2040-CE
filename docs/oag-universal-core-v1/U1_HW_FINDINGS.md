# OAG Universal Core V1 — U1 Hardware Findings

## U1-HW1 initial physical test

Tested artifact commit:

- 6de0306da945a61a764749b9d1b1ea1ab3391bc7

User-confirmed hardware observations:

1. The T29 input path is alive far enough to observe analog movement.
2. Left-stick vertical direction is reversed in the U1-HW1 PC output.
3. Moving the controller from one PIO USB root port to another requires disconnecting and reconnecting Pico power/USB before input resumes.

These observations do **not** mark U1-HW1 as fully HARDWARE VERIFIED.

## U1-HW1A corrective scope

The corrective build changes only the affected lifecycle/orientation behavior:

- XUSB Y axes follow the G2E3-proven orientation before entering UniversalGamepadState.
- Failed XUSB IN transfers are not immediately re-armed during disconnect.
- Generic TinyUSB device-unmount performs idempotent registry/slot cleanup before class close.
- The temporary single PC HID output selects the lowest currently bound logical slot deterministically.
- No Last-Active arbitration is introduced.
- Internal logical slots remain independent.
- No Bluetooth, keyboard, mouse, rumble, authentication, console output or macro engine is added.

Hardware verification remains pending until the replacement UF2 is tested physically.
