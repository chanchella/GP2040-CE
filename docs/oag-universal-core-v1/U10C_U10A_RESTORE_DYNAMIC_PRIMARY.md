# U10C — U10A Restore + Dynamic Primary Controller

U10C restores the exact U10A PC output implementation from
`5cde8e6f5fb406efd9adf76cc2aa1c82988a3fb8`.

The U10B receiver-descriptor experiment is rejected for runtime use.

## Primary rules

- Default Player 1 remains the first connected Bluetooth gamepad.
- Any connected gamepad can become Player 1 by holding Start + Share for 3 seconds.
- Legacy controllers use Start + Back/View/Select as the compatible fallback.
- Xbox Series BLE Consumer Record (0x00B2) is normalized as ButtonShare.
- A manual choice stays active while that controller is connected.
- If it disconnects, routing returns automatically to Bluetooth-first.

## Keyboard and mouse

All connected keyboards and mice are composed only onto PC Output 0, so they
always follow the current primary gamepad after a manual switch.

## Hubs and independent gamepads

USB gamepads use DeviceId + logical slot, not physical-port assignment.
Controllers behind USB hubs are independent sources just like controllers on
the three PIO USB root ports.

OAG keeps eight internal gamepad slots. PC XInput exposes four player outputs,
so at most four controllers can be presented as independent XInput players at
the same time.

## Steam limitation

U10A hardware testing already demonstrated independent Windows XInput indices,
including live input on Slot 1. Steam may still group all interfaces from the
single Pico USB device as one physical controller in its Controller Settings UI.

Making every attached controller appear as a completely separate physical USB
device to every platform requires a different USB architecture (true hub with
independent child device identities) or a host-side virtual-controller layer.
U10C does not risk the stable U10A descriptor to fake that behavior.

## Hardware gate

1. Bluetooth + wired gamepad + keyboard + mouse regress cleanly.
2. Bluetooth is Player 1 by default.
3. Hold Start + Share/View on wired gamepad for 3s: wired becomes Player 1.
4. Keyboard and mouse immediately follow the new wired Player 1.
5. Hold Start + Share on Bluetooth for 3s: Bluetooth becomes Player 1 again.
6. Disconnect a manually selected wired pad: Bluetooth becomes Player 1.
7. Repeat with a gamepad connected through a USB hub.
