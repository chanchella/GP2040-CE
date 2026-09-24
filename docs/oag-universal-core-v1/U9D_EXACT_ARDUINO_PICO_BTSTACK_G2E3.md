# U9D — Exact Arduino-Pico Bluetooth Stack Generation + G2E3 Invariants

U9D combines the two hardware-proven reference lines instead of approximating
either one.

## G2E3 invariants retained

- PIO USB Host is initialized before CYW43.
- The three physical USB roots remain GPIO2/3, GPIO4/5, GPIO6/7.
- A 100 ms USB settle window remains before Bluetooth initialization.
- pico_cyw43_arch_none / async BTstack servicing remains.
- USB keyboard/mouse/gamepad and four PC XInput outputs remain structurally
  unchanged.

## Historical sketch stack generation

The uploaded BluetoothHIDMaster implementation came from Arduino-Pico 6.1.1.
That release pins:

- Pico SDK: 98a542c1a62fb549ffb5d66a3e5892b06276b670 (SDK 2.3.0)
- BTstack: 075a0780f0fad7ff67d58ac19f46e8953656a752
- CYW43 driver: 055d64274b014dd7b1c2fc94d26e8a18face7124

This BTstack generation uses the HIDS Host API:
- hids_host_init
- hids_host_connect
- hids_host_disconnect
- hids_host_descriptor_storage_get_descriptor_*

The older SDK 2.1.1 generation used hids_client_* instead. U9D therefore
switches to the exact SDK/BTstack generation and API family that the historical
working sketch used.

## Pair/discovery behavior

U9C compatibility mechanics are retained:
- local GAP/ATT Device Name service
- HID UUID 0x1812 candidate filter
- passive scan 75/50
- Scan -> Stop -> deferred Connect
- pairing immediately after LE Connection Complete
- HIDS Host connect after pairing/re-encryption
- one-time U9D bond reset marker

## Verification

The first hardware gate is one known-good Bluetooth controller. Four-peer
support remains a software target until the first Bluetooth path is hardware
verified.
