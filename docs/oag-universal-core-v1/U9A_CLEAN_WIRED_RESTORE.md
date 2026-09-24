# U9A — Clean Wired Restore

U9A removes the U8A-U8J Bluetooth runtime experiment from the live firmware
and restores the hardware-verified U6E wired baseline before Bluetooth is
rebuilt.

Restored:
- three PIO USB root ports
- wired XUSB/XGIP controller input
- four PC XInput outputs
- keyboard and mouse host input
- slot 0 keyboard/mouse overlay
- keyboard LED handling
- wired rumble reverse path

Removed from the live firmware:
- U8 Bluetooth runtime implementation
- BLE peripheral GATT persona
- U8 Bluetooth diagnostics
- U8 pairing/security recovery patches

Repository history remains intact. No reset or force-push is used.
