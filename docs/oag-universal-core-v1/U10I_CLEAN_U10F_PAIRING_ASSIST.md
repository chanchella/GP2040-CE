# U10I — Clean U10F Pairing Mode + Wired Pairing Assist

## Baseline

U10I is rebuilt directly from the hardware-good U10F tree:

`7799d270d98ae09e7608f05b15f687a20a459871`

It intentionally does not inherit the U10G/U10H Bluetooth recovery runtime.

## Frozen U10F behavior

Byte-for-byte unchanged:

- true 045E:0719 Xbox 360 Wireless Receiver persona
- independent PC controllers
- host-aware Player 1 routing
- keyboard/mouse follows Player 1
- Start + Share/Back/Guide 3-second primary switching
- receiver rumble and Xbox BLE rumble
- Guide/Home behavior
- USB Hub and three PIO USB roots
- PC XInput device/output implementation
- Bluetooth HID parser

Only Bluetooth discovery/pairing assistance and two USB attach/detach notifications
are added.

## Normal Bluetooth behavior

Outside Pairing Assist, U10F discovery is unchanged:

BLE passive scan -> Classic inquiry -> BLE passive scan

Up to four Bluetooth HID peers remain supported.

## Pairing Assist

Connecting or disconnecting an allow-listed Bluetooth-capable controller over USB
opens a 30-second Pairing Assist window.

During that window:

- BLE discovery has priority
- scanning is active instead of passive
- Classic inquiry is temporarily deferred
- existing connected Bluetooth peers are not disconnected
- no stored bond is proactively deleted

The cable is therefore a discovery accelerator / recovery gesture. It does not
pretend that USB can force every controller radio into Bluetooth Pairing Mode.

For controllers that do not advertise while USB is attached, unplugging the cable
refreshes the full 30-second window; press the controller Pair button and the Pico
is already listening aggressively.

## Stale bond repair

Only the BTstack-defined stale-key condition is repaired automatically:

`ERROR_CODE_PIN_OR_KEY_MISSING`

The recovery follows the official BTstack Central example:

1. read the identity address from the re-encryption event
2. delete only that exact bond
3. request fresh SMP pairing on the live connection
4. start HIDS after pairing succeeds

Generic `AUTHENTICATION_FAILURE` is deliberately not treated as permission to
delete a bond. This avoids the over-aggressive behavior seen in U10G.

## Wired-assist allow list

USB attach/detach Pairing Assist is enabled for known Bluetooth-capable families:

- Microsoft Xbox One / One S / Elite / Elite 2 / Adaptive / Series S|X
- Sony DualShock 4 / DualSense / DualSense Edge
- Nintendo Switch Pro Controller

Unknown wired controllers keep normal U10F behavior.

## Hardware gate

1. Regression: Bluetooth controller + G808 + keyboard + mouse must behave exactly
   as U10F.
2. Bluetooth remains Player 1 unless manually changed.
3. Keyboard/mouse remain with Player 1.
4. Pair the Bluetooth controller to another host, then return it to Pairing Mode
   near the Pico. Stale-key repair must restore HID input.
5. Cable assist: connect the controller over USB, then unplug and press Pair if
   needed. It should be discovered rapidly during the 30-second BLE window.
6. Rumble and Guide/Home must remain correct.

Until those checks pass, U10I is CI-verified only.
