# U3A — Universal Platform Profiles + Generic HID Gamepad Foundation

U3A moves OAG toward the product goal:

> Any supported physical controller, keyboard or mouse is normalized into the
> Universal Core, then emitted through a target-specific platform profile.

U3A is software-foundation only. The hardware-verified PC Xbox 360/XInput
runtime from U2F remains the active firmware output profile and is not changed.

## Platform profile registry

The new PlatformProfileRegistry distinguishes:

- PC XInput 360
- PC Generic HID
- Android Gamepad
- iOS Game Controller
- Nintendo Switch
- PlayStation 3
- PlayStation 4
- PlayStation 5
- Xbox 360 Console
- Xbox One Console
- Xbox Series Console

Each profile declares:

- target wire protocol
- implementation state
- logical-controller count
- feedback capabilities
- authentication requirement
- hardware-verification state

PC XInput 360 remains the default RuntimeAvailable profile.

## Authentication boundary

Modern console authentication is not hidden in the input or mapping layer.

The new AuthRequirement and IPlatformAuthProvider contracts define a separate
role for live official-controller donor/passthrough where a target requires it.

No platform secret, key or donor identity is cloned or fabricated by this
foundation.

## Generic HID gamepad parser

GenericHidGamepadDriver is extracted from the proven Golden design and adapted
to the new UniversalGamepadState ABI.

Properties:

- no heap allocation
- fixed maximum of 96 parsed fields
- HID Report-ID support
- Generic Desktop Gamepad / Joystick / Multi-Axis collection detection
- X/Y/Rx/Ry axes
- Z/Rz ambiguity handled with explicit quirks
- Simulation Accelerator/Brake triggers
- Hat switch to canonical D-pad
- common generic-HID button ordering
- full signed 32-bit canonical axes
- full unsigned 32-bit canonical triggers
- generation-safe DeviceId source tracking

The parser is tested on descriptor and report vectors before it is connected to
the live TinyUSB host callbacks.

## Runtime deliberately unchanged

U3A does not modify:

- PC Xbox 360/XInput USB descriptors
- Windows XInput output
- T29/XUSB host runtime
- reverse rumble
- U2F Golden-style USB host stability behavior
- physical P1/P2/P3 wiring

## Next gate

U3B will connect non-Boot HID Gamepad interfaces from TinyUSB Host into:

Generic HID descriptor
-> GenericHidGamepadDriver
-> DeviceRegistry
-> LogicalSlotManager
-> UniversalGamepadState
-> existing PC Xbox 360/XInput output

This will make ordinary USB HID gamepads use the same PC Xbox 360 output path
without changing the physical controller protocol.
