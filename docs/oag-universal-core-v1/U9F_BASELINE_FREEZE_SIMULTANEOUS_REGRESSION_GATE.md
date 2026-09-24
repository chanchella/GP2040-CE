# U9F — Baseline Freeze + Simultaneous Wired + Bluetooth Regression Gate

## Purpose

U9F freezes the hardware-verified U9E Bluetooth behavior as the known-good
Bluetooth baseline and performs a coexistence regression gate with the already
hardware-verified U6E/U9A wired path.

U9E hardware-verified baseline commit:

`aed44da64f668553e24eb8f4a60a991725318932`

U9F MUST NOT change Bluetooth transport, pairing, discovery, SDK generation,
BTstack generation, CYW43 behavior, HIDS Host dispatch, wired input parsers,
logical-slot routing, keyboard/mouse mapping, XInput descriptors, or rumble
routing.

## Frozen invariants

- Raspberry Pi Pico 2 W / RP2350.
- PIO USB Host roots use DP pins 2, 4 and 6.
- PIO USB Host starts before Bluetooth/CYW43.
- 100 ms settle before CYW43/BTstack startup.
- Pico SDK: `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- BTstack: `075a0780f0fad7ff67d58ac19f46e8953656a752`.
- CYW43 driver: `055d64274b014dd7b1c2fc94d26e8a18face7124`.
- BLE HID host API: `hids_host_*`.
- U9E live HIDS report-dispatch fix remains unchanged.
- U6E/U9A wired behavior remains the safety baseline.
- No Last-Active arbitration.
- Physical controllers remain independently routed by DeviceId + LogicalSlot.

CI enforces that `oag/firmware/src` and `oag/src` are byte-for-byte unchanged
from U9E for this phase.

## Verification states

Before hardware testing:

- SOURCE EXISTS: expected after the U9F commit is created.
- SOFTWARE VERIFIED: expected after static/host checks pass.
- CI VERIFIED: expected only after the GitHub Actions U9F build passes.
- HARDWARE VERIFIED: PENDING until the physical regression matrix below passes.

## Hardware regression matrix

Use the exact U9F UF2 artifact produced by CI.

1. Bluetooth-only sanity
   - Pair/connect the same controller already proven on U9E.
   - Controller LED remains stable.
   - Inputs are visible and responsive in Windows/joy.cpl.

2. Bluetooth + wired controller
   - Keep the Bluetooth controller connected and active.
   - Connect one wired controller.
   - Verify both controllers respond independently with no slot merging.

3. Bluetooth + keyboard + mouse
   - Keep the Bluetooth controller connected.
   - Connect keyboard and mouse.
   - Verify the existing keyboard/mouse-to-Slot-0 mapping still works.
   - Verify Bluetooth controller input continues independently.

4. Full simultaneous gate
   - Bluetooth gamepad + wired gamepad + keyboard + mouse active together.
   - Exercise buttons, sticks, D-pad, keyboard mapping and mouse-to-stick input.
   - Verify no disconnect, stuck input, cross-device overwrite or Last-Active behavior.

5. Three-root coexistence check
   - With Bluetooth still connected, place wired controller, keyboard and mouse
     across the three PIO USB roots (GPIO2/3, GPIO4/5, GPIO6/7).
   - Verify all three roots remain operational together with Bluetooth.

Only after the user confirms this matrix on hardware may U9F be marked
HARDWARE VERIFIED.

## Next phase after PASS

Proceed to Bluetooth multi-device expansion in this order:

1. Two simultaneous Bluetooth gamepads.
2. Three simultaneous Bluetooth gamepads.
3. Four simultaneous Bluetooth devices/gamepads.
4. Bluetooth keyboard and mouse mixtures.

Do not claim any multi-Bluetooth hardware level until it is physically tested.
