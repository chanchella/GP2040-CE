# U2C — Boot HID host integration foundation

This phase connects standard TinyUSB Boot Keyboard and Boot Mouse interfaces
to the OAG Universal Core while preserving the existing XUSB/T29 path.

## Implemented

- Boot keyboard parser:
  - modifier byte;
  - six-key boot report;
  - full 0..255 HID usage bitset;
  - rollover/error report rejection;
  - generation and timestamp tracking.

- Boot mouse parser:
  - buttons;
  - signed relative X/Y;
  - optional wheel;
  - optional horizontal pan;
  - generation and timestamp tracking.

- Device Registry integration:
  - HidKeyboard and HidMouse records use normal generation-safe DeviceId values.
  - fixed-capacity state storage, no heap allocation.

- Continuous HID polling:
  - request first report at mount;
  - re-arm tuh_hid_receive_report() after every received report.

- Multi-device capacity:
  - CFG_TUH_HID raised from 1 compile-anchor slot to 4 HID interfaces.

- Host-health integration:
  - generic TinyUSB mount/unmount now owns root presence tracking;
  - the two-second root watchdog therefore sees XUSB, keyboard and mouse
    devices rather than treating HID devices as missing mounts.

## Deliberately not enabled yet

- Generic non-Boot HID descriptor parsing.
- Keyboard-to-gamepad bindings.
- Mouse buttons-to-gamepad bindings.
- Mouse-to-stick runtime routing.
- PC XInput device-side switch.

Those are layered on top only after the input transport is independently
stable.
