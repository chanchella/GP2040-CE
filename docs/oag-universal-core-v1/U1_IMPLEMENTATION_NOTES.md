# OAG Universal Core V1 — U1 Implementation Notes

Status: U1 IMPLEMENTATION IN PROGRESS

Product identity for the new firmware:

- Manufacturer: OAG
- Product: OAG Abo Gemi Ultra Gaming
- Firmware family: OAG Universal Input Dongle
- Initial board: Raspberry Pi Pico 2 W

## Branch isolation

All U1 development is owned by:

- chatgpt/oag-universal-core-v1

The following remain read-only and are not development inputs:

- golden/oag-g2e3-home-km-led
- antigravity/g2e3-work
- integration/oag-universal-core-v1

No Antigravity commit is merged, cherry-picked, copied, or used as a working baseline.

## U1-A foundation

The transport-independent core contains:

- generation-safe DeviceId
- fixed-capacity DeviceRegistry
- USB transport handle that is not a logical slot
- protocol classification field
- four independent logical gamepad slots
- no Last-Active arbitration
- new UniversalGamepadState
- separate LogicalGamepadState
- PassThroughMapping
- product identity
- host-native tests
- architecture dependency guard
- dedicated GitHub Actions workflow
- independent XUSB parser writing directly into UniversalGamepadState

## U1-B firmware slice

The U1-HW1 firmware source adds:

- Pico 2 W-only firmware target
- deterministic verification of the exact Golden TinyUSB and Pico-PIO-USB SHAs
- explicit build-time patch from two to three Pico-PIO-USB root ports
- P1 D+ GPIO2 / D- GPIO3
- P2 D+ GPIO4 / D- GPIO5
- P3 D+ GPIO6 / D- GPIO7
- standalone XUSB host class adapter
- G2E3/T29 045E:028E fallback behavior
- G2E3/T29 Player-1 startup OUT packet
- DeviceRegistry -> LogicalSlotManager -> UniversalGamepadState
- PassThroughMapping
- one PC Generic HID development output
- build manifest, size report, UF2, ELF and MAP artifacts

Bluetooth is intentionally disabled in U1-HW1. Therefore USB Host starts before any future CYW43/Bluetooth initialization by construction.

The PC development descriptor currently retains legacy G2E3 VID 10C4 / PID 82C0 for continuity only. It is not treated as a console authentication identity or evidence of official platform authorization.

## Verification language

Source being present is SOURCE EXISTS.

Passing host tests and compiling the firmware is SOFTWARE VERIFIED for that scope.

Passing the dedicated workflow on the exact commit is CI VERIFIED.

Nothing becomes HARDWARE VERIFIED until the user physically flashes the exact UF2 and confirms the requested P1/P2/P3 tests.
