# U6E — Four-Player PC XInput Output

U6E closes the gap found during the first U6 hardware test: multiple physical
controllers were being parsed and assigned independent OAG logical slots, but
the live PC platform driver intentionally accepted only logical slot 0.

## Output model

The PC Xbox 360 persona now exposes four independent XInput gameplay
interfaces.

- PC Player 1 <- OAG Logical Slot 0
- PC Player 2 <- OAG Logical Slot 1
- PC Player 3 <- OAG Logical Slot 2
- PC Player 4 <- OAG Logical Slot 3

Windows XInput has four player slots, while OAG keeps eight host-side gamepad
slots for future non-XInput platform/output policies.

The USB host still identifies controllers by transport/interface and protocol.
No controller family is tied to P1, P2, P3, or a hub downstream port. Devices
bind to the first free logical slot regardless of which physical root/hub port
they arrived through.

## Slot 0 overlay

The proven keyboard + mouse FPS mapping remains an overlay on Logical Slot 0.

A physical gamepad in Slot 0 can therefore still be composed with:

- Keyboard -> left stick/buttons/D-pad
- Mouse -> right stick/triggers/buttons

Slots 1 through 3 are independent gamepad outputs and do not inherit the
keyboard/mouse overlay.

## Reverse feedback

Each PC XInput OUT endpoint now has independent rumble state. Rumble received
for Player N is routed back only to the physical controller bound to Logical
Slot N.

XUSB and wired XGIP reverse-rumble paths are supported. Generic HID vendor
rumble remains disabled unless a family-specific safe output driver exists.

## Enumeration behavior

The four PC player interfaces are part of the static USB configuration, so
Windows can show four Xbox 360-compatible controllers even when fewer physical
controllers are attached. Empty OAG logical slots report a neutral state.

This avoids USB re-enumeration every time a controller is attached or removed
and keeps player ordering stable.

## Preserved U6 behavior

U6E does not change:

- three PIO USB roots;
- 120 MHz host clock;
- USB hub support;
- 12-device host capacity;
- eight OAG logical input slots;
- G808 SET_IDLE quirk;
- generic HID structural fallback;
- wired Xbox One/Series XGIP initialization;
- keyboard lock LEDs;
- 6000 us mouse right-stick recenter behavior;
- pinned TinyUSB or Pico-PIO-USB SHAs.

Verification remains CI-only until the exact U6E UF2 is tested on Pico 2 W.
