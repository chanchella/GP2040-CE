# U8A — Integrated I/O Foundation + Live BTstack Host

U8A is built on the hardware-verified U6E USB baseline.

## Protected baseline

The following U6E behavior is intentionally preserved:

- three PIO USB Host roots;
- PIO USB Host initialization before CYW43/Bluetooth;
- 120 MHz USB Host clock;
- USB hubs and broad wired controller classification;
- four independent PC XInput outputs;
- Keyboard + Mouse gamepad overlay on Slot 0;
- keyboard lock LEDs;
- G808 SET_IDLE enumeration quirk;
- wired XUSB/XGIP input;
- slot-scoped PC rumble routing.

The Golden reference remains:
`golden/oag-g2e3-home-km-led`
at commit:
`fd21b6b2dd82dd0b35829d17ab21840bb8e5b37b`.

## Live Bluetooth Host

U8A links the Raspberry Pi Pico SDK / BTstack CYW43 stack in polling mode.

Initialization order:

1. TinyUSB device
2. PIO USB Host
3. CYW43 + BTstack
4. Platform output

The firmware alternates discovery windows between:

- Bluetooth Classic inquiry for Peripheral-class HID devices; and
- BLE scanning for the standard HID Service UUID.

The software budget is six concurrent Bluetooth HCI/HID links. This is a
software allocation, not a hardware-verified simultaneous-device claim.

Classic HID uses BTstack HID Host. BLE uses HID over GATT (HOG/HIDS Host).
Pairing supports Just Works / bonding and legacy Classic 0000 PIN fallback.

For descriptors that prove gamepad structure, Bluetooth reports are normalized
to the same HID shape used by the USB GenericHidGamepadDriver, then flow:

Bluetooth
-> DeviceRegistry
-> DeviceId
-> LogicalSlotManager
-> UniversalGamepadState
-> existing platform output path

No Bluetooth controller is bound to a physical USB port and no Last-Active
arbitration is introduced.

## Driver gates still intentionally separate

U8A does not send guessed vendor initialization/output packets.

The following remain separate hardware/software gates:

- DS4/DualSense family-specific output/rumble/features;
- Nintendo Switch / Joy-Con initialization and subcommands;
- Xbox BLE family-specific feedback/extensions;
- Bluetooth keyboard/mouse canonical runtime;
- Bluetooth peripheral/output advertising profiles.

Generic descriptor-compatible Bluetooth gamepads may already function through
the live U8A path, but each named family still needs hardware validation.

## Touchscreen and Drawing Tablet foundations

U8A adds canonical states and mappers for:

- HID Touchscreen/Digitizer
- HID Pen/Digitizer

Mouse movement is accumulated into absolute 0..32767 X/Y coordinates.

Touch foundation:
- Left Click -> contact down/up
- movement while held -> drag/swipe path

Pen foundation:
- Left Click -> Tip
- Right Click -> Barrel
- Middle Click -> Eraser
- mouse source uses binary pressure
- canonical state already carries pressure and X/Y tilt for future real stylus input

No USB Touch/Pen interface is exposed in U8A yet. This intentionally protects
the hardware-verified U6E USB descriptor persona during the first Bluetooth
coexistence test.

## Output modes foundation

The routing model now defines:

- GAMEPAD
- TOUCHSCREEN
- DRAWING_TABLET
- MOUSE_KEYBOARD
- HYBRID

Bluetooth output/peripheral profiles now include Generic HID Keyboard/Mouse,
Touch Digitizer, and Pen Digitizer contracts, but the peripheral radio runtime
is not activated in U8A.

## Verification

U8A may be marked SOURCE EXISTS / SOFTWARE VERIFIED / CI VERIFIED after the
workflow succeeds.

U8A must remain HARDWARE VERIFIED = false until the exact U8A UF2 is flashed
and USB regression + Bluetooth coexistence are physically tested.
