# U8G — Bluetooth Stage-Coded LED Diagnostic

U8G keeps the U8F Host-only Golden bootstrap and does not change USB
descriptors. The Pico LED exposes the last Bluetooth stage in a repeating
three-second cycle.

- 1 short blink: HCI is working and OAG is searching.
- 2 short blinks: a controller candidate was detected, including a Golden
  stored-remote reconnect attempt.
- 3 short blinks: BTstack accepted the connect request.
- 4 short blinks: the Bluetooth transport link opened.
- 5 short blinks: BLE security pairing or reencryption succeeded.
- 6 short blinks: HID service or descriptor is ready.
- solid LED: at least one HID input report was received.

If a failure occurs at the current stage, the short-blink stage count is
followed by one long pulse.

Examples:

- 1 blink only forever: the controller is never detected.
- 2 short + long: candidate found but connection could not start.
- 3 short + long: connect started but the physical link failed.
- 4 short + long: transport opened but security or Classic HID descriptor
  acquisition failed.
- 5 short + long: BLE security succeeded but HIDS setup failed.
- 6 short only: HID is ready but no report has arrived.
- solid: live Bluetooth HID reports reached OAG.

The diagnostic is intentionally visible-only; no USB CDC/Serial interface is
added, preserving the U6E USB descriptor baseline.
