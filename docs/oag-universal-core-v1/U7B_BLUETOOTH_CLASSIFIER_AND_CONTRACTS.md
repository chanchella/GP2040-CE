# U7B — Bluetooth Classifier + Dual-Role Contracts

U7B extends the generation-safe Bluetooth registry from U7A with explicit
device classification and separate runtime contracts for Bluetooth input and
Bluetooth output.

## Input-side classification

BluetoothDeviceClassifier recognizes:

- Bluetooth SIG HID appearances;
- generic gamepad/joystick HID usages;
- generic keyboard and mouse usages;
- Xbox BLE-family devices;
- DualShock 4;
- DualSense;
- Nintendo Switch / Joy-Con / Pro identities.

Capabilities remain descriptive until a concrete runtime profile is hardware
verified.

## Separate radio roles

IBluetoothInputTransport owns the central/host role:

scan -> discover -> pair -> connect -> receive HID input

IBluetoothOutputPeripheral owns the peripheral role:

select output profile -> advertise -> pair with laptop/mobile -> submit logical
gamepad state -> receive reverse feedback

The two roles are deliberately independent so future multi-role BTstack work
does not couple pairing state or gameplay routing.

## Hardware baseline

U7B is software-only. It does not change the U6D UF2 and does not enable
CYW43/BTstack yet.

The next live gate will preserve the Golden ordering:

PIO USB Host first -> 100 ms settle -> CYW43/BTstack.

That ordering previously mattered for three-root USB Host stability and remains
a hard migration invariant.
