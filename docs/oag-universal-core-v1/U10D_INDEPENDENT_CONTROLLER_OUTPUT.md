# U10D — Independent Controller Output

## Frozen baseline

U10D is based on hardware-verified U10C:

`4357a079ed491309380cbe201f962b5def10d6fd`

The following U10C runtime behavior is frozen:

- Bluetooth-first default primary.
- Start + Share/View 3-second primary selection.
- Keyboard/mouse always follow the current primary.
- Bluetooth Home/Guide.
- Xbox BLE reverse rumble.
- Continuous Bluetooth discovery and four-peer software capacity.
- Three PIO USB root ports.
- USB-hub gamepads remain independent DeviceIds.
- Eight internal logical gamepad slots.

## U10D scope

Only the PC-facing USB output transport changes.

U10D keeps four independent PC outputs but uses the established sequential
multi-interface Xbox 360 gadget layout:

- Player 1: IN 0x81 / OUT 0x01
- Player 2: IN 0x82 / OUT 0x02
- Player 3: IN 0x83 / OUT 0x03
- Player 4: IN 0x84 / OUT 0x04

Every interface has its own interrupt IN and OUT endpoint and a 0x22 custom
descriptor. The 20-byte XInput gameplay report and 8-byte rumble report are
unchanged.

This corrects the earlier U10B experiment, which used the old spaced endpoint
layout rather than the sequential layout used by working multi-controller
Xbox 360 gadget implementations.

## Routing semantics

The U10C logical routing remains unchanged:

1. Current primary -> PC Player 1.
2. Remaining Bluetooth gamepads.
3. Remaining wired/USB-hub gamepads.
4. Up to four routed controllers are exposed concurrently.

Keyboard and mouse are composed only onto Player 1.

Changing the primary swaps routes; it never merges gamepad states.

## Hardware gate

1. Windows Device Manager must enumerate cleanly.
2. XInputGetState must retain independent live slots.
3. Steam Settings should show each active controller independently.
4. A local multiplayer game must receive independent Player 1 and Player 2.
5. Start + Share/View 3s must still move the selected physical controller to
   Player 1.
6. Keyboard and mouse must follow Player 1 after the switch.
7. Rumble must return to the physical controller currently routed to each PC
   output.
8. Repeat with a controller connected through a USB hub.

Until these checks pass on hardware, U10D is CI-verified only.
