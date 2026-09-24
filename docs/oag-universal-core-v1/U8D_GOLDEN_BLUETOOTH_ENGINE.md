# U8D — Golden Bluetooth Engine Restoration

U8D is a focused Bluetooth transport correction based on the hardware-proven
G2E3 Golden implementation.

## Hardware evidence that motivated U8D

U8C proved the CYW43 radio and BLE peripheral stack are alive because Android
could discover and pair with "OAG Gamepad", but Bluetooth controllers still
did not pair to OAG.

## Golden transport behaviors restored

- pico_cyw43_arch_none instead of pico_cyw43_arch_poll
- BTstack SDK async-context services the radio in the background
- 100 ms USB Host -> Bluetooth startup ordering remains
- BLE active scan first
- BTstack run-loop timer rotates BLE -> Classic after 5 seconds
- Classic inquiry returns to BLE when complete
- gap_connect_cancel is used when switching discovery modes
- Classic HID uses HID_PROTOCOL_MODE_REPORT
- legacy PIN requests receive negative PIN instead of forced "0000"
- Just Works / Numeric Comparison remain supported
- disconnect/failure returns immediately to the Golden BLE -> Classic cycle

## Bond migration

U8D performs a one-time migration on first boot:

- deletes stale Classic link keys
- clears stale LE device DB entries
- writes a TLV migration marker

It does not erase bonds on every subsequent boot.

Because of this one-time migration, hosts previously paired to U8C may need to
forget "OAG Gamepad" and pair it again once under U8D.

## BLE peripheral output hardening

Slot 0 state is still exported as BLE HID Gamepad. Report-send requests are now
marshalled onto the BTstack main context with
btstack_run_loop_execute_on_main_thread, matching BTstack's threading model
instead of issuing report scheduling directly from the USB/main firmware loop.

## Protected baseline

The U6E hardware-verified USB path, three PIO roots, multi-XInput output,
Keyboard/Mouse overlay, G808 quirk, wired controller support, and pinned
TinyUSB/Pico-PIO-USB dependencies remain unchanged.
