# U10L — Proven Pico 2 W Three-Port PIO Config + Locked Xbox 360 Output Policy

## Why

U10K's synthetic missing-attach recovery did not restore physical Socket 3.
The next fix therefore moves to the actual PIO resource assignment instead of
adding another watchdog.

A previously hardware-proven three-port Pico 2 W diagnostic used this explicit
PIO USB assignment:

- PIO TX block: 1
- TX state machine: 3
- DMA TX channel: 9
- PIO RX block: 0
- RX state machine: 2
- EOP state machine: 3
- P1 D+ GPIO2 / D- GPIO3
- P2 D+ GPIO4 / D- GPIO5
- P3 D+ GPIO6 / D- GPIO7

U10L ports that exact resource tuple into the current Universal Core host.

## Removed

The failed U10K synthetic missing-attach watchdog is removed. main.cpp is
restored exactly to U10J so Pairing Mode, Player-1 selection and K/M routing
stay on the accepted baseline.

## Permanent PC gamepad output policy

For PC mode, every **supported gamepad** that is successfully parsed from:

- direct USB
- a USB hub
- Bluetooth LE/Classic HID
- XUSB
- wired XGIP
- supported Generic HID gamepad descriptors

is normalized into OAG UniversalGamepadState and routed only through
PcXinputPlatformDriver / PcXinputDevice.

The target-facing gamepad persona remains Xbox 360/XInput receiver semantics.
Generic HID gamepad output to the PC is disabled.

PC XInput exposes four controller children maximum. OAG still owns eight
internal logical gamepad slots.

Keyboard/mouse are not separate gamepad identities; they overlay the current
primary controller. Automatic primary preference remains the first connected
Bluetooth gamepad unless the existing manual primary chord selects another
controller.

This PC output policy is a project invariant from U10L onward and must not be
changed without explicit user direction.

## Frozen

- U10J Pairing Mode
- maximum four Bluetooth peers
- Bluetooth-first Player 1
- Start + Share/Back/Guide primary selection
- keyboard/mouse follows current primary
- Xbox 360 Wireless Receiver target persona
- rumble and Guide
- USB Hub host support
- gamepad parsers and mappings

## Hardware acceptance

Test:
1. Keyboard on P1, P2, P3.
2. Mouse on P1, P2, P3.
3. G808 / supported wired gamepad on P1, P2, P3.
4. USB hub with multiple supported devices.
5. Bluetooth + wired + K/M simultaneously.
6. Confirm every detected gamepad exposed to Windows is an Xbox 360/XInput
   controller child.
7. Confirm first Bluetooth gamepad is Player 1 and K/M follows current primary.
8. Re-test Pairing Mode.

Hardware verification remains pending until physical confirmation.
