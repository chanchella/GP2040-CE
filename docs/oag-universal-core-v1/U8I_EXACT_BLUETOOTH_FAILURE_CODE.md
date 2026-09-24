# U8I — Exact Bluetooth Failure Code over XInput Slot 4

U8I keeps the U8H fourth-XInput diagnostic and exposes the exact failure byte.

## Failure frame

- Button 8 / Start: failure marker.
- Button 7 / Back: Classic transport. If Button 7 is off, transport is BLE.
- Buttons 1,2,3,4,5,6,9,10 encode error bits 0..7 respectively.
- POV shows the failing stage:
  - Up = 1
  - Up+Right = 2
  - Right = 3
  - Down+Right = 4
  - Down = 5
  - Down+Left = 6

For BLE SM pairing failure, the byte is the SMP pairing reason when present,
otherwise the reported HCI status. For other failures it is the exact
BTstack status/reason returned by the relevant event.

This remains Host-only and does not change USB descriptors.
