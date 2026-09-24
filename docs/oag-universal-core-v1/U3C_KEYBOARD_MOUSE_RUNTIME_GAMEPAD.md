# U3C — Live Keyboard + Mouse -> Logical Xbox 360 Controller

U3C connects the existing keyboard, mouse, binding and mouse-aim foundations
to the live firmware.

The Windows-side output remains the same hardware-verified PC Xbox 360/XInput
profile. Physical input may now be:

- XUSB/T29 gamepad;
- Generic USB HID gamepad;
- USB keyboard;
- USB mouse;
- keyboard + mouse together;
- keyboard/mouse overlaid on the current primary physical gamepad.

## Default keyboard profile

- W/S/A/D -> left stick
- Space -> South / A
- E -> East / B
- Q -> West / X
- R -> North / Y
- Tab -> Back
- Enter -> Start
- C -> left-stick click
- F -> right bumper
- Arrow keys -> D-pad

## Default mouse profile

- movement -> right stick
- left button -> right trigger
- right button -> left trigger
- middle button -> right-stick click
- back button -> left bumper
- forward button -> right bumper

The defaults are intentionally isolated in KeyboardMouseGamepadMapper so a
future persisted/WebHID profile can replace them without transport changes.

## Mouse aim pulse

Mouse movement is relative while an analog stick is absolute.

U3C therefore holds the mapped right-stick deflection only for a short pulse
and automatically returns it to center 6000 microseconds after the latest
non-zero mouse delta.

This prevents a stopped mouse from leaving the logical right stick stuck.

## Merge policy

This is not Last-Active arbitration.

The current primary physical gamepad creates the base logical state. Keyboard
and mouse mappings are overlaid on that state. If no physical gamepad exists,
keyboard/mouse can still create a connected logical controller.

## Preserved behavior

- PC Xbox 360/XInput device identity and reports remain unchanged.
- Windows -> T29 reverse rumble remains unchanged.
- Golden 120 MHz host clock behavior remains.
- Golden continuous XUSB receive re-arm remains.
- Generic HID gamepad routing from U3B remains.
