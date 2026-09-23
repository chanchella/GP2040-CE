# U1-HW3 — Two-second root reconciliation watchdog

## User-requested behavior

Every two seconds, OAG checks whether a controller is physically present on
P1/P2/P3 and compares that to what TinyUSB currently considers mounted.

## Safety boundary

This watchdog does **not**:

- reboot Pico;
- deinitialize the USB device side connected to Windows;
- clear endpoint memory manually;
- mutate Pico-PIO-USB root state;
- reset PIO state machines.

It only reads the physical line state using the current upstream
Pico-PIO-USB implementation and, when the physical and logical states differ,
queues ordinary TinyUSB HCD REMOVE/ATTACH events.

## Reconciliation

For three roots:

- stale mounted root: mounted=1, physical=0 -> REMOVE
- missing mounted root: physical=1, mounted=0 -> ATTACH
- matching state -> no action

The reconciliation logic is host-tested independently from the Pico-specific
probe code.

## Timing

The first check runs two seconds after startup, then every two seconds.

## Hardware target

- P1: D+ GPIO2 / D- GPIO3
- P2: D+ GPIO4 / D- GPIO5
- P3: D+ GPIO6 / D- GPIO7

The goal is automatic recovery after moving T29 between roots without
restarting the OAG dongle.
