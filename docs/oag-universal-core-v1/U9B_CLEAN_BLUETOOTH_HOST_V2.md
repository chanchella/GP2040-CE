# U9B — Clean Bluetooth Host V2

U9B is a new Bluetooth implementation built on top of the U9A clean wired
restore. It does not reuse the U8A-U8J runtime/state-machine implementation.

## Baseline preservation

The U6E wired path remains the base:

- PIO USB Host is initialized first.
- three root ports remain GPIO2/3, GPIO4/5, GPIO6/7.
- wired XUSB/XGIP, keyboard and mouse behavior stays intact.
- four PC XInput outputs remain unchanged.
- Bluetooth is fail-soft and is initialized only after a 100 ms USB settle.

## Bluetooth transport source

The transport/pairing sequence intentionally follows the hardware-proven
G2E3 mechanics:

- pico_cyw43_arch_none / SDK async context
- LE active scan
- BLE -> Classic -> BLE discovery cadence
- Classic peripheral-class filtering
- LE HID UUID / appearance filtering
- IO_CAPABILITY_NO_INPUT_NO_OUTPUT
- SM_AUTHREQ_BONDING
- Just Works and Numeric Comparison confirmation
- Classic HID REPORT mode
- negative legacy PIN handling

## Multi-device architecture

The runtime has four independent peer records. Discovery resumes after each
successful HID connection until four peers are active.

Supported input classes at the software level:

- BLE HID gamepad
- Classic HID gamepad
- BLE HID keyboard
- Classic HID keyboard
- BLE HID mouse
- Classic HID mouse

Each accepted HID service receives its own DeviceRegistry identity. Gamepads
bind to independent LogicalSlot entries. Keyboard and mouse states are merged
through the existing U6E slot-0 overlay.

The four-peer budget is SOFTWARE VERIFIED only until physical testing proves
the actual CYW43 scheduling ceiling.

## Fresh start

U9B performs a one-time reset of legacy Classic link keys and the LE device
database, guarded by a new U9 TLV marker. Subsequent U9 boots preserve BTstack
bonding data and rediscover bonded devices normally.

## External project intake

- G2E3 / GP2040-CE: MIT-compatible source and the primary transport reference.
- Pico-PIO-USB: MIT; the existing pinned USB Host dependency is preserved.
- HID-Remapper: MIT; mapping/configuration concepts are reserved for a later
  isolated mapping/config phase.
- BlueRetro: Apache-2.0; controller-family parsing is a reference for future
  family drivers after generic HID is hardware-proven.
- GIMX: GPLv3. No GIMX source code is copied into U9B. Deadzone/ballistic
  concepts may be independently implemented later to avoid accidental GPL
  coupling into this firmware.

## Explicit non-goals in U9B

- Bluetooth peripheral/phone output
- Bluetooth rumble/LED feedback
- console authentication
- descriptor/serial spoofing for bypassing platform protections
- changing the U6E mouse-to-stick algorithm
