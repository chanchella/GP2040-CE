# U2F — Golden USB Host Stability Port

U2F keeps the hardware-verified PC Xbox 360/XInput output from U2E unchanged
and modifies only the USB Host scheduling/lifecycle behavior.

## Imported from the read-only Golden baseline

Golden commit:

`fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b`

Two transport behaviors are carried forward:

1. **120 MHz USB-host system clock**
   - The Golden firmware explicitly selected 120 MHz whenever USB Host was
     enabled to avoid marginal USB timing.
   - U2F applies the same clock before TinyUSB device/host initialization.

2. **Continuous XUSB gameplay IN maintenance**
   - The Golden Universal XInput addon repeatedly called
     `tuh_xinput_receive_report()` whenever the device was mounted and its
     gameplay IN endpoint was ready.
   - U2F adds the same maintenance loop for every bound XUSB logical slot.

## Removed from active runtime

The experimental two-second synthetic root reconciliation watchdog is disabled.
U2F no longer injects synthetic HCD REMOVE/ATTACH events during normal runtime.

## Preserved unchanged

- PC Xbox 360/XInput-compatible device profile.
- XInput 20-byte output report.
- Windows -> T29 reverse rumble.
- Three physical PIO USB roots.
- Boot Keyboard/Mouse host support.
- RP2350-capable Pico-PIO-USB backend.
- Universal Device Registry and logical slot model.

## Hardware acceptance

The transport test remains:

- boot T29 on P1;
- P1 -> P2 without restarting Pico;
- P2 -> P3;
- P3 -> P1;
- same-port unplug/replug.

PC output must continue to enumerate and operate as Xbox 360/XInput throughout.
