# U4A — Nintendo Switch Pro Output Foundation

U4A adds the first non-PC platform report encoder while keeping the live
firmware default on the hardware-verified PC Xbox 360/XInput profile.

## Source reference

The Switch Pro report semantics are derived from the project's read-only
Golden baseline:

- golden commit: fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b
- SwitchPro descriptors and runtime mapping

The Golden branch is not modified.

## Encoder

LogicalGamepadState is converted into a 64-byte Switch Pro input report:

- report ID 0x30
- timestamp
- connection/battery byte
- Y/X/B/A
- L/R/ZL/ZR
- Plus/Minus
- L3/R3
- Home
- D-pad
- two packed 12-bit sticks
- wired/charging-grip bit
- Golden-compatible rumble marker byte

Canonical OAG Y axes are inverted at the Switch wire boundary.

## Status

Nintendo Switch moves from Planned to SoftwareFoundation.

This does not yet claim live Switch console support. Runtime still defaults to
PC_XINPUT_360. The next Switch gate is the USB HID descriptor +
handshake/subcommand state machine, followed by physical console testing.
