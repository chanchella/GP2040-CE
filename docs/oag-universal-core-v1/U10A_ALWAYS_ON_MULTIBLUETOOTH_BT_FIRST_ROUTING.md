# U10A — Always-On Multi-Bluetooth + Bluetooth-First PC Routing

## Frozen hardware baseline

U10A is based on U9G:

`3fffa9484f159df6cfbeb807c3acbdf46bdbe52e`

U9G is user-confirmed HARDWARE VERIFIED for:

- Bluetooth controller live input.
- Xbox BLE Home / Guide.
- Xbox BLE reverse rumble start/stop.
- Simultaneous wired controller + keyboard + mouse coexistence.

U10A must not redesign pairing, the U9E live-report fix, Xbox BLE rumble format,
SDK 2.3.0, BTstack 075a078, CYW43, or the G2E3 USB-before-Bluetooth startup
sequence.

## Goals

1. Keep Bluetooth discovery active whenever fewer than four Bluetooth peers are
   connected.
2. Resume discovery automatically after a disconnect.
3. Permit up to four simultaneous Bluetooth HID peers in any supported mix of
   gamepads, keyboards, and mice.
4. Make the first connected Bluetooth gamepad the sticky primary PC controller
   (Player 1 / XInput Output 0).
5. Prioritize remaining Bluetooth gamepads over wired gamepads for the four
   available PC XInput outputs.
6. Preserve wired devices internally; if a Bluetooth gamepad disconnects, a
   connected wired gamepad can return to a free PC output automatically.
7. Keep reverse rumble routed to the physical controller currently assigned to
   that PC output.

## Discovery behavior

The existing hardware-proven BLE -> Classic -> BLE cadence remains intact.
U10A adds only an idle-state recovery guard. Discovery pauses when the peer
table reaches four entries because there is no connection capacity; if any
peer disconnects, discovery resumes automatically.

This is intentionally safer than scanning indefinitely at full capacity.

## 8 internal slots -> 4 PC XInput outputs

U10A stops assuming that an internal logical slot number is the same as a PC
XInput player number.

PC routing priority is:

1. The first Bluetooth gamepad that connected and is still connected.
2. Other connected Bluetooth gamepads.
3. Connected wired gamepads.

The four-PC-output limit remains unchanged because Windows XInput exposes four
player slots. Additional internal gamepads may remain connected but cannot all
be simultaneously exposed as XInput when more than four gamepads are present.

Keyboard/mouse overlay remains on PC Output 0, now composed over the primary
Bluetooth gamepad when one is connected.

## Steam

U10A deliberately does not change the hardware-verified XInput USB
descriptors. The wired controller already reaches Windows joy.cpl and browser
Gamepad API; changing the USB identity/descriptor only to chase a Steam-specific
enumeration issue would risk the stable baseline. Steam-specific investigation
is deferred unless it can be isolated without changing the working transport.

## Hardware gate

Flash the exact U10A CI artifact and verify:

1. U9G regression: Home, rumble, normal Bluetooth input, wired controller,
   keyboard, and mouse still work.
2. With the wired controller already connected, connect Bluetooth gamepad #1:
   it must become Player 1 / primary.
3. Add Bluetooth devices one at a time up to four total peers.
4. Disconnect one Bluetooth peer and confirm discovery becomes available again
   and a replacement peer can connect.
5. With two or more Bluetooth gamepads, verify independent input and independent
   rumble for every PC-visible gamepad.
6. Verify keyboard and mouse may be Bluetooth peers without stopping discovery
   while capacity remains.

Until this matrix is physically tested, U10A is CI VERIFIED only.
