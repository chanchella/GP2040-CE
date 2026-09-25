# U10F-PM1 — Hardware Baseline Lock + Latency Optimization Gate

## Locked baseline

Runtime baseline:

`64195844e28ded0126d3965594705585a31d469d`

Source lineage:

- U10F: `7799d270d98ae09e7608f05b15f687a20a459871`
- Pairing logic only from U10J: `52f727530a2e1d17199ff939b69446745f131c65`

Branch:

`chatgpt/oag-universal-core-v1`

The later branch commit `645b2b4cf046c44a31a0e80efd0cefec13c19f5d` changes workflow/package handling only and does not alter runtime source relative to the locked U10F-PM1 firmware baseline.

## Hardware-verified behavior

User hardware testing on U10F-PM1 confirms:

- wired gamepads read correctly
- 2.4 GHz / wireless gamepads read correctly
- Bluetooth gamepads read correctly
- keyboard mapping works correctly as gamepad input
- mouse mapping works correctly as gamepad input
- Windows `joy.cpl` enumerates the virtual controllers
- `joy.cpl` reads normal buttons and axes correctly

These behaviors are regression-locked.

## Protected invariants

All latency work must preserve:

1. U10F USB behavior
2. current Bluetooth Pairing Mode
3. USB Hub support
4. up to 4 Bluetooth HID peers
5. Bluetooth-first Player 1
6. Keyboard/Mouse follows current Primary only
7. every supported gamepad outputs to PC as Xbox 360/XInput
8. rumble + Guide/Home routing
9. independent physical controllers / no merge
10. no Last-Active arbitration
11. PIO USB Host initialization before Bluetooth/CYW43
12. PC persona `045E:0719`
13. four PC XInput outputs / eight internal OAG slots
14. mouse recenter pulse remains 6000 us unless separately hardware-gated

## Verification classification

### HARDWARE VERIFIED

The behavior listed under Hardware-verified behavior above.

### SOURCE VERIFIED

- target-facing Xbox 360 Wireless Receiver controller IN endpoints advertise `bInterval = 1` ms
- main firmware loop has no frame sleep / fixed-delay limiter
- USB HID reports are handled and the next receive is re-armed immediately
- XUSB/XGIP host reports are handled and IN is re-armed immediately
- output submission is event-driven from the incoming controller report path

### SOFTWARE / CI VERIFIED

Must be re-established for every latency candidate before hardware testing.

### HARDWARE RECHECK REQUIRED

Guide/Home and reverse-rumble remain protected baseline requirements, but their current U10F-PM1 behavior must not be newly labeled hardware-verified from `joy.cpl`, because `joy.cpl` is not a sufficient Guide/Rumble test surface.

## Latency phase policy

Latency optimization is transport-specific. Do not claim end-to-end 1 ms for every physical controller.

### PC target output

Target-facing USB XInput receiver polling:

`1 ms / 1000 Hz`

This is already present in U10F-PM1 and must not be changed merely to create a new latency commit.

### USB XUSB / XGIP input

Current path is continuously armed / event-driven. Do not override a physical controller's USB endpoint interval without device-specific hardware evidence.

### Generic HID / keyboard / mouse input

Current path re-arms HID receive immediately after each successful report. Host scheduling still follows the physical endpoint's declared interval. Do not globally rewrite endpoint timing.

### 2.4 GHz receiver input

Treat as USB HID/XUSB according to the receiver persona. Preserve device quirks such as the 2563:0575 SET_IDLE skip. Optimize only when a measured software-added delay exists.

### Bluetooth Classic HID

Reports are asynchronous through BTstack. Preserve Classic HID compatibility and bonding. Do not apply BLE-only timing controls to Classic peers.

### Bluetooth LE HID

This is the first legitimate latency candidate because BLE connection interval can dominate controller-to-Pico latency.

Any optimization must be:

- BLE-only
- best-effort
- non-fatal if rejected
- no bond clearing
- no disconnect-on-reject
- no change to Pairing Assist semantics
- no change to four-peer capacity
- hardware-gated before promotion

## Next candidate

`U10F-PM1-L1`

Scope:

Best-effort BLE HID low-latency connection-parameter request only, if supported by the pinned BTstack API. No USB, routing, output descriptor, KM mapping, Pairing Mode, rumble, or Guide architecture changes.

Promotion requires:

1. build/CI pass
2. wired regression
3. hub regression
4. 2.4 GHz regression
5. Bluetooth Classic regression
6. BLE gamepad regression
7. multi-controller coexistence
8. keyboard/mouse regression
9. Guide/Home regression
10. rumble regression

Until hardware passes, U10F-PM1 remains the only locked hardware baseline.
