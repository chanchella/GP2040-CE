# U8B — Always-On Bluetooth Input Discovery

U8B fixes the U8A hardware result where USB remained stable but Bluetooth
controllers did not pair.

The hardware-verified U6E USB path is unchanged.

## Golden behaviors restored

- PIO USB Host starts first.
- CYW43/BTstack starts only after a 100 ms settling period.
- Bluetooth init failure is fail-soft and retried every second.
- BLE uses active scan, not passive scan.
- BLE and Classic discovery alternate continuously while capacity remains.
- Disconnect or failed immediate connect returns to discovery.
- Local Bluetooth identity is OAG Abo Gemi Ultra Gaming.
- On-board CYW43 LED provides Bluetooth state diagnostics.

## BLE candidate discovery

A BLE device can enter the HID connection path when it advertises one of:

- HID Service UUID 0x1812;
- standard HID Appearance 0x03C0..0x03C4;
- a controller-oriented local name including Xbox, Wireless Controller,
  DualSense, Pro Controller, Joy-Con, Nintendo, 8BitDo, GameSir, PXN,
  MOCUTE, IPEGA, Gamepad, or Controller.

The HID descriptor remains the final classifier after connection.

## LED states

- medium blink: scanning/inquiry;
- fast blink: connection attempt;
- solid on: at least one Bluetooth connection.

## Role boundary

U8B is Bluetooth INPUT/HOST. It continuously looks for controllers.

Bluetooth OUTPUT/PERIPHERAL — where the Pico itself appears in the Bluetooth
menu of Android/Windows/tablets/laptops as a gamepad, mouse, keyboard,
touch/pen device — remains a separate runtime gate. U8B deliberately does not
pretend that discoverable-only mode is a working peripheral profile.
