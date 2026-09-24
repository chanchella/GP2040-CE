# U10K — Three-Root Missing-Attach Recovery

## Hardware evidence

U10J preserved the desired Bluetooth Pairing Mode and the U10F-style
Player-1/keyboard/mouse behavior, but the third direct PIO USB root did not
enumerate attached devices reliably.

The configured hardware roots remain:

- P1: D+ GPIO2 / D- GPIO3
- P2: D+ GPIO4 / D- GPIO5
- P3: D+ GPIO6 / D- GPIO7

The build-time Pico-PIO-USB root count remains exactly three.

## U10K change

U10K does not rewrite the USB backend. It uses the existing physical root probe
and HCD reconciliation hook only as a missed-CONNECT safety net.

Every 250 ms firmware compares:

- physical PIO line presence
- TinyUSB mounted-root state

A physically present root must remain missing for 2 seconds before U10K
synthesizes an ATTACH event. If enumeration still does not complete, that root
is retried no more often than every 5 seconds.

### Safety boundary

U10K never synthesizes REMOVE events.

Normal disconnect, endpoint lifecycle, hub handling, XInput/HID handling and
hot-plug removal remain owned by upstream Pico-PIO-USB + TinyUSB.

## Frozen behavior

Byte-for-byte frozen from U10J:

- Bluetooth Pairing Mode / stale-key handling
- BluetoothHostV2
- Bluetooth HID parser
- max four Bluetooth peers
- first Bluetooth gamepad automatic Player 1 preference
- manual Start + Share/Back/Guide primary selection
- keyboard/mouse follows the current primary
- Xbox 360 Wireless Receiver persona
- rumble / Guide
- USB hub support
- USB PIO backend and pins
- XInput/XGIP/HID drivers

Only main-loop root attach recovery is added.

## Hardware gate

1. Keyboard P1 -> P2 -> P3.
2. Mouse P1 -> P2 -> P3.
3. G808 / wired controller P1 -> P2 -> P3.
4. USB hub on each direct root where practical.
5. Bluetooth + wired + keyboard + mouse simultaneously.
6. Pairing Mode regression.
7. Bluetooth remains automatic Player 1; K/M follows the selected primary.
8. Multi-Bluetooth capacity remains four peers.

Hardware verification is pending the user's physical test.
