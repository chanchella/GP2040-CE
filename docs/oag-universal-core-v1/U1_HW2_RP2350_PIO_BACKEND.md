# U1-HW2 — RP2350 PIO USB backend refresh

## Physical evidence

The stable U1-HW1C firmware reads the T29 correctly and has correct stick
orientation, but moving the controller between P1/P2/P3 still requires a
dongle restart.

The failed U1-HW1D auto-recovery experiment is not used as a baseline.

## Upstream finding

The previous pinned Pico-PIO-USB revision:

- `37965f8895fffb4ffacace86e1f731f908dc18e0`

predates multiple host fixes from the upstream project, including an RP2350
E9 erratum workaround whose upstream change explicitly addresses PIO USB host
failure to detect device disconnection.

The target board is Raspberry Pi Pico 2 W / RP2350.

## New pin

Pico-PIO-USB is now pinned to upstream:

- repository: `sekigon-gonnoc/Pico-PIO-USB`
- commit: `5a37a66dc5d3fbe0ef3cdbeda923a757440f984f`

This revision also contains later host reliability work such as disconnect
hang prevention, transaction retry improvements, endpoint lifecycle APIs,
RP2350 support, and receive-length hardening.

## OAG delta

OAG no longer patches the PIO USB runtime disconnect/reconnect state machine.
Only one deterministic dependency edit remains:

- `PIO_USB_ROOT_PORT_CNT: 2 -> 3`

The three physical roots remain:

- P1: D+ GPIO2 / D- GPIO3
- P2: D+ GPIO4 / D- GPIO5
- P3: D+ GPIO6 / D- GPIO7

The old `interval_override` compatibility shim is removed because the new
upstream dependency no longer requires it.

## Verification target

The hardware acceptance test is:

1. boot with T29 on P1;
2. verify normal gameplay input;
3. move P1 -> P2 without resetting Pico;
4. move P2 -> P3 without resetting Pico;
5. move P3 -> P1 without resetting Pico;
6. repeat same-port unplug/replug.

This phase changes the Host backend only. PC output remains the known
development HID profile until the hot-plug transport gate is closed.
