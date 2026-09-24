# U8F — Golden First-Link Bootstrap

U8F isolates the first Bluetooth controller connection as closely as possible
to the hardware-proven G2E3 Golden firmware.

## Critical correction

U8D/U8E performed a one-time bond cleanup. That can create an asymmetric
bond state if the controller still remembers the Pico while the Pico erased
its local key.

U8F performs no bond deletion.

## Exact Golden remembered-device compatibility

U8F reads the same persistent TLV tags used by G2E3:

- OAGB (0x4F414742): stored BLE remote address + address type
- OAGC (0x4F414743): stored Classic remote address + profile

When BTstack reaches HCI_STATE_WORKING:

1. reconnect saved BLE remote first when present;
2. otherwise reconnect saved Classic remote;
3. only if neither is available, start BLE -> Classic discovery.

This reproduces Golden's reconnect-first behavior while forwarding the
resulting descriptor/reports into the new DeviceId/LogicalSlot architecture.

## Temporary resource isolation

For this first-link hardware gate the BTstack compile-time resource limits are
returned to Golden values:

- HCI connections: 3
- HID Host connections: 2
- HIDS clients: 2
- GATT clients: 2
- L2CAP channels: 8

The modern runtime arrays remain multi-device capable. After first-link
pairing is hardware verified, resource limits will be increased stepwise and
validated with 2, 3, and more simultaneous Bluetooth controllers.

## Bluetooth Output

Still disabled. U8F is controller Host/Input only.
