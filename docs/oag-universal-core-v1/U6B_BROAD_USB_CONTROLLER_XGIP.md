# U6B — Broad USB Controller Detection + Wired XGIP Input

U6B expands live host compatibility without changing the proven target-facing
PC Xbox 360/XInput profile.

## Central USB classifier

A single OAG classifier now recognizes controller families by both known
identity and protocol signature.

Known profiles include:

- hardware-proven 045E:028E XUSB-compatible path;
- Redragon G808 2563:0575;
- GIGAMAX / common 0079:0006 HID family;
- ShanWan 20BC:0055 and 20BC:5500 fallback HID identities;
- Xbox One S 045E:02EA;
- Xbox One-family 24C6:542A;
- generic XUSB signature FF/5D/01;
- generic XGIP signature FF/47/D0;
- generic HID.

Known-device quirks remain data, not scattered driver patches.

## Generic HID structural fallback

Some inexpensive controllers expose valid gamepad axes/buttons under a
vendor/composite top-level collection instead of declaring Game Pad or
Joystick correctly.

U6B first attempts standards-compliant descriptor parsing. If that fails, it
runs a structural evidence pass. A descriptor is only forced into gamepad mode
when it contains:

- X axis;
- Y axis; and
- buttons, a hat switch, or multiple additional axes.

This broadens Chinese/2.4GHz receiver compatibility without claiming every
vendor HID interface as a controller.

## Wired Xbox One / Series input

The XInput host class now accepts XGIP-style interfaces and exposes them as a
separate OAG protocol.

The runtime adds the Golden-proven host initialization sequence:

POWER
-> SYSTEM_INIT when required
-> EXTRA_INPUT when required
-> LED
-> AUTH_DONE
-> READY

Normal command 0x20 input packets are normalized into UniversalGamepadState.
Guide state is carried by XGIP virtual-key packet 0x07 and preserved across
normal state packets.

## Reverse feedback

For a primary wired XGIP controller, normalized Windows rumble is emitted as a
GIP rumble packet after the controller reaches READY.

## Preserved baseline

U6B does not modify the PC Xbox 360 device descriptors or output report
semantics. T29, keyboard/mouse mapping, hub capacity and the Golden host timing
rules remain intact.
