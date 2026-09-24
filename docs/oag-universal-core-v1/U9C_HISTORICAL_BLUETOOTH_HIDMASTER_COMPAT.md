# U9C — Historical BluetoothHIDMaster Compatibility Bootstrap

U9C is based on the user's historical working BluetoothHIDMaster path rather
than another U8-style pairing patch.

Hardware baseline remains U9A/U6E for wired USB, keyboard, mouse and XInput.

## Compatibility changes

1. Install a minimal local GAP/ATT Device Name service before HCI power-on.
   This mirrors BluetoothHCI::setBLEName() + install().
2. Use the historical BLE scan cadence:
   - passive scan
   - interval 75
   - window 50
3. Accept BLE candidates only when advertising data contains HID service
   UUID 0x1812.
4. Never call gap_connect() directly inside GAP_EVENT_ADVERTISING_REPORT.
   The event only captures one candidate. The main firmware poll path then:
   - stops scan
   - cancels discovery
   - issues gap_connect()
5. Pairing is still requested immediately after LE Connection Complete.
6. HIDS discovery starts after pairing/re-encryption success using the pinned
   SDK's hids_client API, which is the predecessor of the newer hids_host API.
7. The U9 bond-reset marker is bumped so first U9C boot clears prior U9B bond
   residue once. Later U9C boots preserve bonds.

## Why this phase exists

The historical sketch_sep19a_3usb path used Arduino-Pico BluetoothHIDMaster,
which separates Scan -> Stop -> Connect and installs a local GAP ATT server.
U9B connected immediately from inside the advertising callback and did not
install local ATT data.

U9C tests these lifecycle differences without changing Pico SDK, TinyUSB,
Pico-PIO-USB, USB descriptors or the proven wired pipeline.

## Verification

Four-peer structures remain in the architecture, but U9C's first hardware gate
is one known-good Bluetooth controller. Multi-peer claims remain unverified
until that first transport path is proven on hardware.
