# U9G — Xbox BLE Guide + Reverse Rumble

## Baseline

U9G starts from the exact U9F artifact that passed the simultaneous hardware
regression:

`4bcb0b21f848d2f7cd324e873bc55272dab46ad7`

User-confirmed U9F hardware result:

- Bluetooth gamepad works simultaneously with the wired controller.
- Keyboard and mouse continue working at the same time.
- No regression was reported in the U6E/U9A wired safety baseline.

Therefore U9F coexistence is HARDWARE VERIFIED.

## Scope

U9G fixes only two missing Xbox BLE semantics observed after U9F:

1. Home / Guide input.
2. Windows XInput rumble routed back to the Bluetooth controller.

Pairing, discovery, SDK generation, BTstack generation, CYW43 startup,
G2E3 USB-before-Bluetooth ordering, the U9E report-dispatch fix, wired parsers,
logical-slot assignment and PC XInput descriptors are not redesigned.

## Guide fix

The historical hardware-working BluetoothHIDMaster path handled Xbox Home as
Consumer-page usage `0x0223` (AC Home), independently of the ordinary gamepad
button bitmap.

U9G mirrors that behavior in BluetoothHidParserV2:

- Consumer `0x0223` press sets `ButtonGuide`.
- Consumer `0x0223` release clears `ButtonGuide`.
- Ordinary button reports preserve Consumer Guide state when that report does
  not itself carry Button usage 13.
- If a controller reports Guide directly as Button usage 13, that native
  button field remains authoritative.

## Xbox BLE rumble fix

U9G adds a transport-level BLE HID output-write primitive to BluetoothHostV2
and keeps the Xbox-specific packet construction in the firmware feedback
routing layer.

For descriptor-classified Xbox BLE gamepads only:

- HID Output Report ID: `0x03`.
- Payload length: 8 bytes.
- XInput motor magnitudes 0..255 are scaled to Xbox BLE 0..100.
- Weak motor enable mask: bit 0.
- Strong motor enable mask: bit 1.
- Stop command uses actuator mask `0x0F` with zero magnitudes.
- Active command uses duration `0xFF`, delay `0x00`, loop count `25`.
- BTstack busy responses retain the pending command and retry after 50 ms.
- Other HID families still receive no guessed vendor output reports.

This intentionally ports the previously hardware-working BluetoothHIDMaster
rumble behavior rather than inventing a new packet format.

## Verification status before hardware test

- SOURCE EXISTS: expected after commit.
- SOFTWARE VERIFIED: pending CI.
- CI VERIFIED: pending CI.
- HARDWARE VERIFIED: pending user test.

## Required U9G hardware gate

1. Flash the exact CI U9G UF2.
2. Confirm the same Xbox Bluetooth controller still pairs and its normal input
   still works.
3. Press Home/Guide and confirm Windows/XInput receives Guide.
4. Trigger Windows/game rumble and confirm both start and stop work.
5. Keep wired controller + keyboard + mouse connected during the test and
   confirm U9F coexistence remains intact.

Only after these checks may U9G be marked HARDWARE VERIFIED.
