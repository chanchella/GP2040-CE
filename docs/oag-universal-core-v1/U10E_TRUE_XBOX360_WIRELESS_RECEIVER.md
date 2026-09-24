# U10E — True Xbox 360 Wireless Receiver Persona

## Baseline

U10C remains the hardware-verified behavioral baseline:

`4357a079ed491309380cbe201f962b5def10d6fd`

U10D is rejected by hardware testing.

U10E preserves U10C Bluetooth, USB Host, routing, Home/Guide, rumble forwarding,
three-root/hub support, and dynamic primary selection. Only the target-facing
USB persona and the minimum empty-slot presence semantics change.

## Why U10B/U10D were wrong

A genuine Xbox 360 Wireless Receiver is not four copies of a wired controller.
The real 045E:0719 receiver exposes eight vendor interfaces:

- four controller interfaces, protocol 0x81
- four auxiliary interfaces, protocol 0x82

The controller interfaces use EP pairs 81/01, 83/03, 85/05 and 87/07.
The auxiliary interfaces use 82/02, 84/04, 86/06 and 88/08.

Its total configuration descriptor is 321 bytes.

## Receiver protocol implemented

Device -> host:

- presence: 08 80 (present) or 08 00 (absent), exactly 2 bytes
- controller announce: 00 0F 00 F0..., 29 bytes
- input: 00 01 00 F0 + wireless Xbox payload, 29 bytes
- battery/state reply: 00 00 00 10 C0

Host -> device:

- status request: 08 00 0F C0...
- battery request: 00 00 00 40...
- capabilities request: 00 00 02 80...
- LED/player command: 00 00 08 4x
- rumble: 00 01 0F C0 00 <strong> <weak>

Presence is heartbeated every second while a routed controller is active so a
late-opening Steam/SDL client does not miss controller creation.

## Player mapping

U10C routing stays authoritative:

1. current primary -> Receiver Controller 1
2. remaining Bluetooth gamepads
3. remaining wired / USB-hub gamepads
4. up to four active receiver child controllers

Keyboard and mouse remain composed only onto Receiver Controller 1.

Start + Share/View for 3 seconds still changes the current primary.

## Hardware gate

1. Windows must enumerate as Xbox 360 Wireless Receiver (045E:0719).
2. With one physical controller routed, Steam must show one Xbox controller.
3. With Bluetooth + wired, Steam must show two independent Xbox controllers.
4. Browser tester / joy.cpl must show independent inputs.
5. Local multiplayer must receive separate Player 1 / Player 2.
6. Start + Share/View 3s must move the selected physical pad to Player 1.
7. Keyboard/mouse must follow Player 1 only.
8. Rumble must return to the correct physical pad.
9. Repeat with the second pad connected behind a USB hub.

Until those pass, U10E is CI-verified only.
