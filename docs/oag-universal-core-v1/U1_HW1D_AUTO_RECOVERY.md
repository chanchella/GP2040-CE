# U1-HW1D — Automatic PIO USB host recovery

## Why this exists

Physical testing confirmed:

- XUSB/T29 input works.
- Axis orientation is correct.
- Moving the controller between P1/P2/P3 can still leave the PIO USB host
  state stuck until the entire dongle is restarted.

The pinned Pico-PIO-USB backend exposes multiple root ports by switching the
same PIO state machines between roots. TinyUSB performs a reset plus a long
contact-debounce/enumeration sequence per attach. If a root remains physically
connected after a failed or lost enumeration attempt, no second attach edge is
guaranteed, so the root can stay present but unmounted indefinitely.

## Recovery design

U1-HW1D adds an OAG-owned recovery state machine above the PIO HCD:

1. Read the physical FS/LS line state of P1/P2/P3.
2. Track which root TinyUSB reports as the mounted XUSB root.
3. If a physical root is occupied but no occupied root matches the mounted
   XUSB root for one second, treat the host state as stale.
4. Freeze the PIO-USB timer IRQ briefly.
5. Clear endpoint/root runtime bookkeeping for the host roots only.
6. Queue TinyUSB root REMOVE events.
7. Queue ATTACH only for roots that are physically occupied.
8. Let normal TinyUSB reset/debounce/enumeration run again.
9. Retry at a bounded interval until XUSB mounts.

The USB DEVICE side connecting OAG to Windows is never deinitialized. This is
not a Pico reboot and should not make Windows lose the OAG device.

This recovery is intentionally U1-specific while only XUSB gameplay input is
enabled. U2 will generalize recovery ownership so multiple simultaneous HID,
keyboard, mouse and gamepad devices are preserved independently.
