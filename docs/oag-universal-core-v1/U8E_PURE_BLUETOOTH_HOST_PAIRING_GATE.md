# U8E — Pure Bluetooth Host Pairing Gate

U8E intentionally removes Bluetooth Output/Peripheral runtime from the current
hardware test. The phone/tablet path is postponed until controller pairing is
proven.

## Why

U8C/U8D proved the CYW43 radio works because Android discovered and paired
with OAG Gamepad, but Bluetooth controllers did not pair to the Pico.

For U8E, the Bluetooth radio has one job only: act as a controller host.

## Golden G2E3 behavior retained

- pico_cyw43_arch_none / BTstack async-context
- USB Host starts first, Bluetooth after 100 ms
- BLE active scan first
- BTstack timer rotates BLE -> Classic
- gap_connect_cancel when switching modes
- Classic HID REPORT mode
- negative legacy PIN request
- Just Works / Numeric Comparison
- one-time bond migration
- disconnect/failure returns to discovery

## Additional sequencing fix

The modern multi-device code previously restarted discovery immediately after
Classic HID_CONNECTION_OPENED. U8E waits until HID_DESCRIPTOR_AVAILABLE before
starting discovery for the next controller, matching the Golden single-device
sequencing while still preserving multi-device arrays and LogicalSlot routing.

## Multi-controller architecture remains

U8E does not revert to the Golden single Bluetooth slot.

Each successful controller still receives:

Bluetooth transport
-> connection handle
-> DeviceId
-> LogicalSlot
-> independent UniversalGamepadState

After one controller is fully initialized, discovery resumes for the next one
while software connection capacity remains.

Bluetooth Output/Peripheral will be restored only after this host pairing gate
is hardware verified.
