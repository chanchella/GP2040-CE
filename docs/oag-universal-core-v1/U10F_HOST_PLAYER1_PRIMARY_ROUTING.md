# U10F — Host Player-1 Aware Primary Routing

## Baseline

U10E commit:

`b662eb5a456be889f9e49e10a49367bbbb36f967`

Hardware feedback confirms that the true Xbox 360 Wireless Receiver persona,
independent Bluetooth/wired controllers, keyboard and mouse operate together.
The remaining U10E defect is an ownership mismatch: firmware assumed receiver
child 0 was Windows Player 1, while xusb22 can assign XInput user numbers
dynamically.

U10F preserves the U10E receiver persona and fixes only that ownership layer.

## Host player assignment

Xbox 360 Wireless Receiver LED commands encode the host player number:

- 0x42 or 0x46 -> Player 1
- 0x43 or 0x47 -> Player 2
- 0x44 or 0x48 -> Player 3
- 0x45 or 0x49 -> Player 4

PcXinputDevice records these commands and surfaces assignment changes to
FirmwareCore.

FirmwareCore routes the selected physical primary to whichever receiver child
Windows actually assigned to Player 1.

## Primary rules

1. Manual 3-second selection wins while that controller remains connected.
2. Otherwise the first connected Bluetooth gamepad is preferred.
3. If no Bluetooth gamepad exists, the first routable wired gamepad is primary.
4. Keyboard and mouse overlay the same receiver child Windows calls Player 1.
5. Rumble remains reverse-routed through that child to the physical controller
   occupying it.

## Chord compatibility

The three-second primary chord accepts:

- Start + Share
- Start + Back / Select
- Start + Guide

The Guide fallback is intentional for generic/PS3-style HID families such as
2563:0575 where a dedicated Share usage is not consistently exposed.

## Frozen from U10E

Unchanged:

- 045E:0719 receiver identity
- eight-interface descriptor
- 0x81 controller / 0x82 auxiliary topology
- endpoint layout
- 29-byte wireless input packet
- presence heartbeat
- receiver rumble
- Bluetooth transport/discovery
- G2E3 init ordering
- three PIO USB roots and hub support

## Hardware gate

1. Connect Bluetooth controller first: it must be Windows/Steam Player 1.
2. Connect G808: Bluetooth must remain Player 1.
3. Keyboard/mouse must control that same Player-1 virtual controller.
4. Hold Start + Select/Share/Guide on G808 for 3 seconds.
5. G808 must become Player 1 and keyboard/mouse must move with it.
6. Repeat chord on Bluetooth: Bluetooth must regain Player 1 and K/M.
7. Both pads must remain independent in Steam/game.
8. Rumble must return to the correct physical pad after each switch.

Until these pass, U10F is CI-verified only.
