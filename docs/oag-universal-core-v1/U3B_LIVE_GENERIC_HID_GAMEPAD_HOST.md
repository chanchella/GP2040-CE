# U3B — Live Generic HID Gamepad Host -> PC Xbox 360 Output

U3B connects the GenericHidGamepadDriver foundation from U3A to the live
TinyUSB Host path while preserving the U2F PC Xbox 360/XInput device output.

## Runtime path

```
USB HID Gamepad / Joystick
  -> TinyUSB HID Host
  -> HID report descriptor
  -> GenericHidGamepadDriver
  -> DeviceRegistry (ProtocolKind::HidGamepad)
  -> LogicalSlotManager
  -> UniversalGamepadState
  -> PassThroughMapping
  -> existing PC Xbox 360/XInput output
```

This means the physical input controller no longer has to be XUSB/T29 in order
to produce the current PC Xbox 360-compatible logical controller output.

## Preserved paths

- T29 / XUSB host input
- Boot keyboard input
- Boot mouse input
- three PIO USB roots
- 120 MHz Golden USB-host clock
- Golden continuous XUSB input re-arm
- PC Xbox 360/XInput device descriptors
- Windows -> T29 reverse rumble

## Generic HID handling

The live host integration:

- ignores Boot Keyboard and Boot Mouse for gamepad classification;
- parses non-Boot HID report descriptors;
- recognizes Game Pad, Joystick and Multi-Axis top-level collections;
- creates generation-safe HidGamepad DeviceId entries;
- binds gamepads through the same LogicalSlotManager used by XUSB;
- preserves neutral reports/releases;
- supports HID Report IDs;
- raises CFG_TUH_HID to 8 interfaces for composite/multi-device headroom.

Known Golden quirks are retained narrowly:

- 2563:0575 and 0079:0006 may use Z/Rz as right-stick axes;
- 20BC:0055 and 20BC:5500 may require forced gamepad classification.

## Not yet claimed

- Generic HID physical rumble routing.
- DS4/DualSense/Switch-specific feature reports.
- Bluetooth controller parsing.
- console output profiles other than the current PC XInput profile.
- console authentication.

Those remain independent gates so the verified PC output is not destabilized.
