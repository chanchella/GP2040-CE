# U8C — BLE HID Gamepad Peripheral + Host Role Split

U8C makes the Pico visible as a real BLE HID gamepad while preserving the
Bluetooth input host path and the hardware-verified U6E USB baseline.

## Peripheral output

The Pico advertises:

- Bluetooth name: OAG Gamepad
- HID Service UUID: 0x1812
- HID Appearance: Gamepad (0x03C4)

The composed OAG Slot 0 state is sent as a BLE HID gamepad report. This means
the existing keyboard/mouse-to-gamepad overlay and any controller bound to
Slot 0 can also drive a paired Android/Windows BLE host.

## Role separation

LE connections are separated by role:

- HCI_ROLE_MASTER: OAG initiated the connection to a remote controller. This
  remains the Bluetooth input/HIDS Client path.
- HCI_ROLE_SLAVE: a phone/tablet/PC connected to OAG. This is the BLE HID
  peripheral/HIDS Device path.

A phone connecting to OAG can no longer be mistaken for a controller input.

## Hardware gate

U8C specifically tests:

1. OAG Gamepad appears in Android/Windows Bluetooth discovery.
2. Pairing completes.
3. Slot 0 produces BLE HID gamepad input.
4. Wired USB controllers + keyboard + mouse remain unchanged.
5. Bluetooth input-host discovery remains available for controller testing.

The exact simultaneous Central+Peripheral behavior remains hardware-gated.
