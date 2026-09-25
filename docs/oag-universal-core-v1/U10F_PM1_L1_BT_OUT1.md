# U10F-PM1-L1-BT-OUT1 — BLE Platform Gamepad Output

## Parent / rollback reference

TRUE GOLDEN parent (never modified):

`59182f30c2028f77c059666d6473487d90a82b64`

BT-OUT1 is a forward candidate only. Rollback is always the exact TRUE GOLDEN SHA above.

## Goal

Expose the Pico 2 W as a standard Bluetooth LE HID Gamepad to a platform while preserving every existing input path and the existing PC Xbox 360/XInput USB output.

Primary composed state is mirrored in parallel:

`USB / Hub / 2.4G / Bluetooth controller / Keyboard+Mouse -> Universal Primary -> PC XInput + BLE HID Gamepad`

## Open-source / standards basis

- Pinned BlueKitchen BTstack HIDS Device API already shipped in the exact Pico SDK dependency.
- Raspberry Pi Pico BTstack BLE HID example architecture (`pico_btstack_make_gatt_header` + HIDS Device).
- Generic HID Gamepad report-map conventions also cross-checked against ESP32-BLE-Gamepad.
- Bluetooth SIG assigned Appearance `0x03C4` = Gamepad.

No external Bluetooth stack is introduced.

## Isolation rules

- Existing Bluetooth Host peers remain in `peers_` and retain the four-controller budget.
- The platform/peripheral connection has a separate connection handle and is never inserted into `peers_`.
- U10F-PM1-L1 BLE controller-input low-latency request remains intact.
- USB Host, Hub, Generic HID, XUSB, XGIP, PC 045E:0719 descriptors, Player-1 routing, KM mapping, Guide and rumble paths are not redesigned.
- BLE platform output currently mirrors the Primary only.
- BT-OUT1 does not yet implement Bluetooth-platform rumble.

## First hardware gate

1. Phone/PC/tablet Bluetooth scan shows `OAG Universal Pad` / `OAG Universal Gamepad`.
2. Pair and connect.
3. Platform recognizes it as a game controller.
4. USB/2.4G controller through Pico controls the platform.
5. Keyboard+Mouse mapping controls the same Bluetooth gamepad.
6. Bluetooth controller -> Pico -> Bluetooth platform works concurrently.
7. Existing PC USB XInput path still works.
8. Existing USB Hub and multiple input controllers still work.

Until those checks pass, BT-OUT1 is CI-verified candidate only, not a replacement for TRUE GOLDEN.
