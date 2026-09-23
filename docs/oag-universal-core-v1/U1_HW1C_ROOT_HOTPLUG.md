# U1-HW1C — PIO USB root hot-plug lifecycle

Pinned dependency:

- Pico-PIO-USB: `37965f8895fffb4ffacace86e1f731f908dc18e0`

Physical evidence before this change:

- XUSB/T29 input works after boot.
- X/Y orientation is now correct.
- Moving the controller between P1/P2/P3 still requires restarting the
  dongle even after the first endpoint-close cleanup patch.

## Root cause being tested

The pinned multi-root implementation emits endpoint FAILED events for every
queued endpoint and then emits DISCONNECT. With three roots, those stale
endpoint events can race TinyUSB's root removal and the next root attach.

The additional roots also do not receive the same explicit host-mode/runtime
initialization fields as root 0.

## U1-HW1C deterministic patch

The build preparation now:

1. keeps exactly three roots;
2. fully clears endpoint objects when a device is closed;
3. converts physical hot-unplug into one atomic root removal event;
4. clears the disconnected root's endpoint/status bookkeeping immediately;
5. re-arms a newly connected root from a clean runtime state before TinyUSB
   enumeration;
6. initializes added roots explicitly as HOST roots.

This remains a transport-layer patch only. No mapping, slot arbitration,
Bluetooth, platform authentication, or PC output protocol is changed here.

Hardware verification requires P1 -> P2 -> P3 -> P1 movement without
restarting Pico power.
