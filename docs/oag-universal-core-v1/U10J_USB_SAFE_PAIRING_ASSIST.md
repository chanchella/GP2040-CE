# U10J — Pairing Preserved, USB Host Isolated

U10J is a minimal corrective patch over U10I.

Hardware feedback on U10I:
- Bluetooth pairing behavior was accepted and must be preserved.
- keyboard/mouse worked only on USB root ports 1 and 2.
- USB root port 3 did not respond.
- wired controllers were not usefully detected even on ports 1 and 2.

## Rule

Pairing assistance must never run while a controller is mounted over USB.

A supported Bluetooth-capable controller now behaves as follows:

1. Plugging it into USB uses pure U10F wired behavior. No Bluetooth discovery
   restart is triggered.
2. Unplugging it opens the 30-second BLE-only Pairing Assist window.
3. Press Pair on the controller. The Pico is already prioritizing BLE.
4. Pairing Assist uses U10F's exact passive scan type, interval 75, window 50.
5. Stale-key recovery remains U10I's exact BTstack PIN_OR_KEY_MISSING-only flow.

Speed comes from avoiding the Classic inquiry half of the normal discovery
cadence during the assist window, not from more aggressive active scanning.

## Frozen from U10F

CI freezes:
- usb_host.cpp / usb_host.h
- USB descriptors and TinyUSB config
- Bluetooth HID parser
- PC receiver/XInput output
- Player-1 routing
- keyboard/mouse runtime and mappings

main.cpp is mechanically checked to equal U10F after removing exactly two
controller-detach Pairing Assist calls.

## Hardware gate

Test after flash/reboot:
1. Keyboard on P1/P2/P3.
2. Mouse on P1/P2/P3.
3. G808 or wired controller on P1/P2/P3.
4. Bluetooth + wired controller + keyboard + mouse simultaneously.
5. Pairing Mode remains functional.
6. Cable recovery: plug supported controller, unplug it, press Pair; Bluetooth
   should be caught during the 30-second BLE-only window.
