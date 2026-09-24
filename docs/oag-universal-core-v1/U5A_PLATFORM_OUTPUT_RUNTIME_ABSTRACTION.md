# U5A — Platform Output Runtime Abstraction

U5A removes the remaining direct dependency between the live firmware Core and
the PC Xbox 360/XInput device implementation.

## Runtime boundary

The firmware now submits LogicalGamepadState through IPlatformOutputDriver.

Current live implementation:

PcXinputPlatformDriver
-> PcXinputDevice
-> hardware-verified PC Xbox 360/XInput USB profile

The adapter exposes:

- platform identity;
- capabilities;
- authentication requirement;
- initialize / poll;
- logical-slot submission;
- normalized rumble feedback.

## Preserved hardware behavior

U5A is an architectural refactor only.

It does not change:

- the PC XInput USB descriptors;
- VID/PID compatibility identity;
- XInput input report layout;
- Windows rumble decoding;
- T29/XUSB host support;
- Generic HID gamepad support;
- Keyboard/Mouse mapping;
- Golden 120 MHz USB-host timing;
- Golden continuous XUSB receive re-arm.

The live default profile remains PC_XINPUT_360.

## Why this gate matters

Future platform drivers can now implement the same runtime contract:

- Nintendo Switch
- PlayStation 3
- PlayStation 4
- Xbox One / Series XGIP
- future verified PS5/DualSense output

without changing the input registry, mappings, keyboard/mouse logic or logical
controller pipeline.

Closed-console profiles still require their declared official-donor
authentication provider before they can become RuntimeAvailable.
