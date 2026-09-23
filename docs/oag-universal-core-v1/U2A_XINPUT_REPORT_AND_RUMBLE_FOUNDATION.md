# U2A — PC XInput report and reverse-feedback foundation

This phase is software-only and does not switch the current USB device
descriptor yet.

## Added

- Native 20-byte Xbox 360/XInput gameplay report model.
- LogicalGamepadState -> XInput report encoder.
- Full basic gamepad buttons and D-pad.
- Guide button.
- Two 8-bit analog triggers.
- Four signed 16-bit sticks.
- Canonical OAG Y orientation is inverted back to XInput wire orientation.
- Two-motor XInput rumble OUT decoder.
- Host tests that round-trip the known T29/XUSB packet through:
  XUSB input -> Universal state -> pass-through mapping -> XInput output.
- Tests for the two-second three-root reconciliation planner.

## Golden reference

The report layout and rumble packet format are derived from the project's
read-only Golden baseline at commit:

`fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b`

The Golden branch is not modified.

## Next integration gate

After this software foundation is green, the next phase connects the encoder
to a TinyUSB vendor/XInput device driver and routes decoded rumble back through
the feedback path toward the physical controller.
