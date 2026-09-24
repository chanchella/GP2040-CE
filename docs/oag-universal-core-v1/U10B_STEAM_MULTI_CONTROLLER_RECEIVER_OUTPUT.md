# U10B — Steam Multi-Controller Receiver Output Experiment

## Why U10B exists

U10A is preserved at:

`5cde8e6f5fb406efd9adf76cc2aa1c82988a3fb8`

User hardware verification established that:

- Bluetooth Player 1 works.
- A wired gamepad works simultaneously.
- Wired keyboard and mouse work simultaneously.
- Windows XInputGetState reports XInput indices 0..3 connected.
- Independent input was observed on XInput index 1.
- Steam Settings still presents only one connected Xbox 360 Controller.

Therefore U10B treats this as a PC output-enumeration problem, not an input
transport or logical-routing problem.

## Scope lock

Only these runtime source files may change from U10A:

- `oag/firmware/src/usb_descriptors.cpp`
- `oag/firmware/src/pc_xinput_device.cpp`

Bluetooth discovery, pairing, Home/Guide, Xbox BLE rumble, USB Host, physical
gamepad parsers, keyboard, mouse, U10A routing and reverse-feedback routing are
frozen.

## Descriptor experiment

U10A repeated the normal wired Xbox 360 custom descriptor (type 0x21) four
times. Windows exposed four XInput indices, but Steam grouped the composite
device as one controller.

U10B switches the four PC-facing interfaces to the established receiver-style
multi-controller layout:

- Vendor interface class/subclass/protocol remains FF/5D/01.
- Custom interface descriptor becomes length 0x14, type 0x22.
- Player endpoint pairs are 81/01, 83/03, 85/05 and 87/07.
- Input packets remain the existing 20-byte Xbox 360 report.
- Output rumble packets remain the existing 8-byte Xbox 360 report.
- VID/PID remain the already-working compatibility identity 045E:028E.
- The serial namespace changes from OAG-* to OAG-RCV-* so Windows creates a
  fresh PnP instance instead of reusing U10A descriptor state.

This direction is based on known multi-controller Xbox 360 receiver/gadget
implementations that expose up to four controller interfaces from one USB
gadget. U10B reimplements only the descriptor shape needed for OAG; it does not
import an external runtime.

## Hardware gate

After flashing the exact U10B artifact:

1. Windows must enumerate the device without Device Manager errors.
2. XInputGetState must still show the expected active slots.
3. Steam Settings > Controller should list more than one controller when more
   than one OAG gamepad route is active.
4. A local-multiplayer Steam game should receive independent Player 1 and
   Player 2 input.
5. Rumble must return to the correct physical controller.
6. Bluetooth Home/Guide, Bluetooth rumble, wired gamepad, keyboard and mouse
   must regress cleanly.

If Windows enumeration fails or Steam still groups the interfaces, revert to
the preserved U10A artifact and continue with a different PC output transport.
