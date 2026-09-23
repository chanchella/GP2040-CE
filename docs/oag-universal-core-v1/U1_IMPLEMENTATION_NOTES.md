# OAG Universal Core V1 — U1 Implementation Notes

Status: U1 STARTED

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

## U1-A scope

This first implementation slice establishes the transport-independent core before the hardware transport is attached:

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

## Verification language

U1-A is not a hardware milestone.

Passing host tests means SOFTWARE VERIFIED for the core scope only.

The later U1-B firmware slice must add:

- Pico 2 W firmware target
- deterministic Pico-PIO-USB three-root dependency preparation
- TinyUSB device output
- XUSB/T29 input adapter
- PC Generic HID output driver
- UF2/ELF/MAP artifact manifest
- physical P1/P2/P3 test instructions

Nothing becomes HARDWARE VERIFIED until the user reports a successful physical test.
