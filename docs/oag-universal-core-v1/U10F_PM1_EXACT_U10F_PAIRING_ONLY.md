# U10F-PM1

This firmware is intentionally not a forward port of U10K/U10L.

Runtime baseline: exact U10F commit 7799d270d98ae09e7608f05b15f687a20a459871.

Only pairing-related runtime differences from U10F are allowed:

1. bluetooth_host_v2.h — exact U10J pairing-isolated version.
2. bluetooth_host_v2.cpp — exact U10J pairing-isolated version.
3. main.cpp — exact U10J file, whose U10F delta is only two wired-gamepad
   detach notifications into Bluetooth Pairing Assist.

No wired attach notification is used.

All USB Host, PIO USB, Hub, XInput Host, Generic HID parser, K/M mapping,
receiver descriptors, PC XInput device, Player-1 routing, rumble and Guide
runtime remain exactly U10F.

PC target persona remains the U10F Xbox 360 Wireless Receiver implementation
(045E:0719). Any supported gamepad that OAG successfully parses is normalized
to the universal gamepad state and emitted through the existing U10F Xbox
360/XInput receiver children. Generic HID gamepad target output remains off.

Hardware verification is pending.
