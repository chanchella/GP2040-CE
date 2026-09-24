# U6A — Multi-Controller Host Capacity + USB Hub Foundation

U6A establishes the host-side capacity required for the product goal without
changing the proven PC Xbox 360 output profile.

## TinyUSB host budget

- 12 non-hub USB device addresses.
- 4 USB hubs.
- 12 simultaneous HID interfaces.
- 8 simultaneous XInput/XUSB interfaces.
- 1024-byte enumeration buffer for larger HID report descriptors.
- 128-byte HID IN/OUT buffers.

The larger enumeration buffer is intentional: many modern and inexpensive
controllers expose report descriptors larger than the previous 256-byte
budget, which can otherwise make TinyUSB skip the report descriptor entirely.

## Logical controller budget

LogicalSlotManager now owns 8 independent gamepad slots.

The PC runtime still exposes one hardware-verified Xbox 360/XInput controller
today. U6A expands input-side identity and routing capacity first so multiple
physical controllers can be parsed independently before later multi-output
profiles are enabled.

## Device registry

DeviceRegistry capacity is raised to 12 and gains XgipXboxOne as a distinct
input protocol kind.

## Preserved baseline

U6A does not alter:

- target-facing Xbox 360 descriptors/reports;
- T29 parser semantics;
- keyboard/mouse mapping;
- rumble reverse path;
- Golden 120 MHz host clock behavior;
- three direct PIO USB roots.
