# U6C — Keyboard Output LEDs

U6C adds the first reverse HID output path for connected USB keyboards while
preserving the keyboard-to-controller mapping introduced earlier.

## Standard lock LEDs

OAG now owns a per-keyboard lock LED state:

- Num Lock
- Caps Lock
- Scroll Lock

The initial OAG keyboard state enables Num Lock. This mirrors a common PC
default and gives immediate visible confirmation that the keyboard Output
Report path is functioning.

Lock keys toggle on a rising edge only. Holding a key does not repeatedly
toggle the state.

## USB output transport

Keyboard LED reports use the standard HID SET_REPORT(Output) control transfer:

- report ID 0
- HID_REPORT_TYPE_OUTPUT
- one-byte standard LED bitmap

The runtime keeps:

- desired state;
- in-flight state;
- applied state;
- asynchronous transfer pending state.

If the HID control endpoint is busy, OAG retries later instead of dropping the
LED update or flooding repeated SET_REPORT requests.

## Scope

U6C intentionally covers the standard lock LED protocol only.

Full RGB/backlight control is often vendor-specific and must be implemented by
per-family drivers/quirks after the base keyboard transport is stable.

## Preserved baseline

Keyboard and mouse remain mapped into the same logical controller. PC output
remains the hardware-verified Xbox 360/XInput profile.
