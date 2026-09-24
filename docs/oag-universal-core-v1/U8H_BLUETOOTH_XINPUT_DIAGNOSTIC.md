# U8H — Bluetooth Diagnostic over Existing XInput Slot 4

The Pico onboard LED is not reliable on the user's hardware, so U8H exposes
the Bluetooth stage through the already-existing fourth XInput interface.

No USB descriptor changes are made.

Open Windows joy.cpl and inspect the fourth OAG/Xbox 360-compatible controller.

Mapping:

- A = stage 1: HCI working / searching
- B = stage 2: controller candidate detected
- X = stage 3: BTstack accepted connect request
- Y = stage 4: transport link opened
- LB = stage 5: BLE security pairing/reencryption succeeded
- RB = stage 6: HID service/descriptor ready
- Start + one of the above = failure at that stage
- Guide + A+B+X+Y+LB+RB = live HID input report reached OAG

This overlay temporarily owns XInput output slot 3 (the fourth Windows
controller) for diagnostics only. Slots 0-2 retain normal behavior.
